#include "index.hpp"
#include <unordered_map>

using namespace polymer;

///////////////////////////////////////
//   Compile-Time Constant Hashing   //
///////////////////////////////////////

using HashValue = uint64_t;
constexpr HashValue hash_offset_basis = 0x84222325;
constexpr HashValue hash_prime_multiplier = 0x000001b3;

namespace detail 
{
    // Helper function for performing the recursion for the compile time hash.
    template <std::size_t N>
    inline constexpr HashValue ConstHash(const char(&str)[N], int start, HashValue hash) 
    {
        // Perform the static_cast to uint64_t otherwise MSVC complains, about integral constant overflow (warning C4307).
        return (start == N || start == N - 1) ? hash : ConstHash(str, start + 1, static_cast<HashValue>((hash ^ static_cast<unsigned char>(str[start])) * static_cast<uint64_t>(hash_prime_multiplier)));
    }
}  // namespace detail

// Compile-time hash function.
template <std::size_t N>
inline constexpr HashValue ConstHash(const char(&str)[N])
{
    return N <= 1 ? 0 : detail::ConstHash(str, 0, hash_offset_basis);
}

// Functor for hashable types in STL containers.
struct Hasher 
{
    template <class T>
    std::size_t operator()(const T& value) const { return Hash(value); }
};

//////////////////
//   Entities   //
//////////////////

// An Entity is an uniquely identifiable object in the Polymer runtime.
using Entity = uint64_t;
constexpr Entity NULL_ENTITY = 0;

//////////////////////////////////////
//   Custom TypeId Implementation   //
//////////////////////////////////////

using TypeId = uint64_t;

template <typename T>
const char * GetTypeName() 
{
    assert(false);
    // If you get a compiler error that complains about TYPEID_NOT_SETUP, then you have not added 
    // POLYMER_SETUP_TYPEID(T) such that the compilation unit  being compiled uses it.
    return T::TYPEID_NOT_SETUP;
}

template <typename T>
TypeId GetTypeId() { return T::TYPEID_NOT_SETUP; }

template <typename T>
struct TypeIdTraits { static constexpr bool kHasTypeId = false; };

#define POLYMER_SETUP_TYPEID(Type)             \
template <>                                    \
struct TypeIdTraits<Type> {                    \
    static constexpr bool kHasTypeId = true;   \
};                                             \
template <>                                    \
    inline const char* GetTypeName<Type>() {   \
    return #Type;                              \
}                                              \
template <>                                    \
    inline TypeId GetTypeId<Type>() {          \
    return ConstHash(#Type);                   \
}                                          

 /////////////////////////
 //   TypeId Registry   //
 /////////////////////////

 // Used for SFINAE on other types we haven't setup yet (stl containers)
 template <typename T>
 struct has_typename { static const bool value = true; };

 class type_name_generator 
 {
     template <typename T>
     static auto generate_impl() -> typename std::enable_if<has_typename<T>::value, std::string>::type
     {
         return GetTypeName<T>();
     }
 public:
     /// Returns the name of the specified type |T|.
     template <typename T>
     static std::string generate()  { return generate_impl<T>(); }
 };

 // Basic Types
 POLYMER_SETUP_TYPEID(bool);
 POLYMER_SETUP_TYPEID(float);
 POLYMER_SETUP_TYPEID(double);
 POLYMER_SETUP_TYPEID(int8_t);
 POLYMER_SETUP_TYPEID(uint8_t);
 POLYMER_SETUP_TYPEID(int16_t);
 POLYMER_SETUP_TYPEID(uint16_t);
 POLYMER_SETUP_TYPEID(int32_t);
 POLYMER_SETUP_TYPEID(uint32_t);
 POLYMER_SETUP_TYPEID(int64_t);
 POLYMER_SETUP_TYPEID(uint64_t);

 // Linalg & Polymer Types
 POLYMER_SETUP_TYPEID(float2);
 POLYMER_SETUP_TYPEID(float3);
 POLYMER_SETUP_TYPEID(float4);
 POLYMER_SETUP_TYPEID(int2);
 POLYMER_SETUP_TYPEID(int3);
 POLYMER_SETUP_TYPEID(int4);
 POLYMER_SETUP_TYPEID(uint2);
 POLYMER_SETUP_TYPEID(uint3);
 POLYMER_SETUP_TYPEID(uint4);
 POLYMER_SETUP_TYPEID(float2x2);
 POLYMER_SETUP_TYPEID(float3x3);
 POLYMER_SETUP_TYPEID(float4x4);
 POLYMER_SETUP_TYPEID(Frustum);
 POLYMER_SETUP_TYPEID(Pose);
 POLYMER_SETUP_TYPEID(Bounds2D);
 POLYMER_SETUP_TYPEID(Bounds3D);

 // Provide a consistent way to retrieve an Entity to which a component belongs. 
 class Component 
 {
     Entity ent;
 public:
     explicit Component(Entity e) : ent(e) { }
     Entity get_entity() const { return ent; }
 };

 // Hash functor for Components so they can be used in unordered containers. It uses the Component's Entity as the key value.
 struct ComponentHash 
 {
     Entity operator()(const Component& c) const { return c.get_entity(); }
 };

 // Systems are responsible for storing the component data instances associated with Entities.
 // They also perform all the logic for manipulating and processing their Components.
 // This base class provides an API for an EntityFactory to associate Components with Entities in a data-driven manner.
 struct EntityFactory;
 struct System : public non_copyable
 {
     EntityFactory * factory;

     explicit System(EntityFactory * f) : factory(f) { }
     virtual ~System() { }

     // The Hash of the actual type used for safely casting the DefinitionType to a concrete type for extracting data.
     using DefinitionType = HashValue;

     // Associates Component(s) with the Entity using the serialized data
     virtual void create(Entity e, DefinitionType type, void * data) { }

     // Disassociates all Component data from the Entity.
     virtual void destroy(Entity e) { }

     // Helper function to associate the System with DefType in the EntityFactory.
     // Example usage: RegisterDef(this, Hash("MyComponentDef"));
     template <typename S>
     void RegisterDef(S * system, DefinitionType type) { RegisterDef(GetTypeId<S>(), type); }

     void RegisterDef(TypeId system_type, DefinitionType type);
 };

 struct EntityFactory
 {
     using SystemMap = std::unordered_map<TypeId, System*>;                 // System TypeId to System instance map.
     using TypeMap = std::unordered_map<System::DefinitionType, TypeId>;    // ComponentDef type (hashed) to System TypeId map.
     using TypeList = std::vector<System::DefinitionType>;                  // ComponentDef type list used during the entity creation process.
     SystemMap systems_;                // Map of TypeId to System instances.
     TypeMap type_map_;                 // Map of ComponentDef type (hash) to System TypeIds.
     TypeList types_;                   // List of ComponentDef types used during the creation process.
     Entity entity_generator_ { 0 };    // Autoincrementing value to generate unique Entity IDs.

     void RegisterDef(TypeId system_type, HashValue def_type) 
     {
         type_map_[def_type] = system_type;
     }

     Entity create() 
     {
         const Entity entity = ++entity_generator_;
         return entity;
     }
 };

 // Associates the System with the DefType in the EntityFactory.
 void System::RegisterDef(TypeId system_type, DefinitionType type) { factory->RegisterDef(system_type, type); }

 template <typename T>
 bool verify_typename(const char * name) 
 {
     return type_name_generator::generate<T>() == std::string(name);
 }

IMPLEMENT_MAIN(int argc, char * argv[])
{
    std::cout << "verify: bool - " << verify_typename<bool>("bool") << std::endl;
    std::cout << "verify: uint64_t - " << verify_typename<uint64_t>("uint64_t") << std::endl;
    std::cout << "verify: float2 - " << verify_typename<float2>("float2") << std::endl;
    std::cout << "verify: Bounds2D - " << verify_typename<Bounds2D>("Bounds2D") << std::endl;

    return EXIT_SUCCESS;
}