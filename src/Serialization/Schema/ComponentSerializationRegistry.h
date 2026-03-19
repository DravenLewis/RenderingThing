/**
 * @file src/Serialization/Schema/ComponentSerializationRegistry.h
 * @brief Declarations for ComponentSerializationRegistry.
 */

#ifndef SERIALIZATION_SCHEMA_COMPONENT_SERIALIZATION_REGISTRY_H
#define SERIALIZATION_SCHEMA_COMPONENT_SERIALIZATION_REGISTRY_H

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "neoecs.hpp"
#include "Serialization/Schema/PrefabSceneSchemas.h"

namespace Serialization {

/// @brief Represents the ComponentSerializationRegistry type.
class ComponentSerializationRegistry {
    public:
        using ComponentRecord = JsonSchema::EntitySnapshotSchemaBase::ComponentRecord;
        using SerializePayloadFn = std::function<bool(const NeoECS::ECSComponent* component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* outError)>;
        using DeserializePayloadFn = std::function<bool(NeoECS::ECSComponent* component, JsonUtils::JsonVal* payload, int version, std::string* outError)>;
        using GetComponentFn = std::function<NeoECS::ECSComponent*(NeoECS::ECSComponentManager* manager, NeoECS::ECSEntity* entity)>;
        using EnsureComponentFn = std::function<bool(NeoECS::GameObject* wrapper, std::string* outError)>;

        /// @brief Holds data for SerializerEntry.
        struct SerializerEntry {
            std::string typeName;
            int version = 1;
            GetComponentFn getComponent;
            EnsureComponentFn ensureComponent;
            SerializePayloadFn serializePayload;
            DeserializePayloadFn deserializePayload;
        };

        /**
         * @brief Registers serializer.
         * @param entry Value for entry.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool registerSerializer(SerializerEntry entry, std::string* outError = nullptr);

        /**
         * @brief Registers typed serializer.
         * @param typeName Name used for type name.
         * @param version Value for version.
         * @param serializePayload Value for serialize payload.
         * @param deserializePayload Value for deserialize payload.
         * @param ensureComponent Value for ensure component.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        template<typename TComponent>
        bool registerTypedSerializer(
            const std::string& typeName,
            int version,
            std::function<bool(const TComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* outError)> serializePayload,
            std::function<bool(TComponent& component, JsonUtils::JsonVal* payload, int version, std::string* outError)> deserializePayload,
            EnsureComponentFn ensureComponent = EnsureComponentFn(),
            std::string* outError = nullptr
        ){
            SerializerEntry entry;
            entry.typeName = typeName;
            entry.version = version;
            entry.getComponent = [](NeoECS::ECSComponentManager* manager, NeoECS::ECSEntity* entity) -> NeoECS::ECSComponent* {
                if(!manager || !entity){
                    return nullptr;
                }
                return manager->getECSComponent<TComponent>(entity);
            };

            if(ensureComponent){
                entry.ensureComponent = std::move(ensureComponent);
            }else{
                entry.ensureComponent = [](NeoECS::GameObject* wrapper, std::string* error) -> bool {
                    if(!wrapper){
                        if(error){
                            *error = "Null GameObject wrapper while ensuring component.";
                        }
                        return false;
                    }
                    if(wrapper->hasComponent<TComponent>()){
                        return true;
                    }
                    if(!wrapper->addComponent<TComponent>()){
                        if(error){
                            *error = "Failed to add missing component to entity.";
                        }
                        return false;
                    }
                    return wrapper->hasComponent<TComponent>();
                };
            }

            entry.serializePayload =
                [serializePayload](const NeoECS::ECSComponent* component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
                    const TComponent* typed = dynamic_cast<const TComponent*>(component);
                    if(!typed){
                        if(error){
                            *error = "Component type mismatch during serialization.";
                        }
                        return false;
                    }
                    return serializePayload(*typed, doc, payload, error);
                };

            entry.deserializePayload =
                [deserializePayload](NeoECS::ECSComponent* component, JsonUtils::JsonVal* payload, int payloadVersion, std::string* error) -> bool {
                    TComponent* typed = dynamic_cast<TComponent*>(component);
                    if(!typed){
                        if(error){
                            *error = "Component type mismatch during deserialization.";
                        }
                        return false;
                    }
                    return deserializePayload(*typed, payload, payloadVersion, error);
                };

            return registerSerializer(std::move(entry), outError);
        }

        /**
         * @brief Checks whether serializer.
         * @param typeName Name used for type name.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool hasSerializer(const std::string& typeName) const;
        /**
         * @brief Registers ed count.
         * @return Computed numeric result.
         */
        size_t registeredCount() const { return orderedSerializers.size(); }

        bool serializeEntityComponents(
            NeoECS::ECSComponentManager* manager,
            NeoECS::ECSEntity* entity,
            std::vector<ComponentRecord>& outRecords,
            std::string* outError = nullptr
        ) const;

        bool deserializeEntityComponents(
            NeoECS::ECSContext* context,
            NeoECS::ECSComponentManager* manager,
            NeoECS::ECSEntity* entity,
            const std::vector<ComponentRecord>& records,
            std::string* outError = nullptr
        ) const;

        bool serializeComponentRecord(
            const std::string& typeName,
            const NeoECS::ECSComponent* component,
            ComponentRecord& outRecord,
            std::string* outError = nullptr
        ) const;

        bool deserializeComponentRecord(
            NeoECS::ECSContext* context,
            NeoECS::ECSComponentManager* manager,
            NeoECS::ECSEntity* entity,
            const ComponentRecord& record,
            std::string* outError = nullptr
        ) const;

        static ComponentSerializationRegistry CreateDefault();

    private:
        bool deserializeComponentRecordWithWrapper(
            NeoECS::GameObject* wrapper,
            NeoECS::ECSComponentManager* manager,
            NeoECS::ECSEntity* entity,
            const ComponentRecord& record,
            std::string* outError
        ) const;

        std::vector<SerializerEntry> orderedSerializers;
        std::unordered_map<std::string, size_t> serializerIndexByType;
};

/**
 * @brief Registers default component serializers.
 * @param registry Value for registry.
 * @param outError Output value for error.
 */
void RegisterDefaultComponentSerializers(ComponentSerializationRegistry& registry, std::string* outError = nullptr);
/**
 * @brief Builds the default component serialization registry.
 * @return Reference to the resulting value.
 */
ComponentSerializationRegistry& DefaultComponentSerializationRegistry();

} // namespace Serialization

#endif // SERIALIZATION_SCHEMA_COMPONENT_SERIALIZATION_REGISTRY_H
