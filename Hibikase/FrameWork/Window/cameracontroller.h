#pragma once

#include <Window\inputmapping.h>
#include <Window\inputsystem.h>

#include <glm/glm.hpp>

namespace HApp
{

class ZWCameraController final
{
public:
    ZWCameraController();

    void Update(const ZWInputSystem& inputSystem, const ZWInputMapping& inputMapping, float deltaTime);

    void SetPosition(const glm::vec3& position);
    void SetRotation(float yaw, float pitch);
    void SetProjection(float fieldOfView, float nearPlane, float farPlane);
    void SetMoveSpeed(float moveSpeed);
    void SetLookSensitivity(float lookSensitivity);
    void SetZoomSensitivity(float zoomSensitivity);

    const glm::vec3& GetPosition() const;
    const glm::vec3& GetFrontVector() const;
    float GetYaw() const;
    float GetPitch() const;
    float GetFieldOfView() const;
    glm::mat4 BuildViewMatrix() const;
    glm::mat4 BuildProjectionMatrix(float aspectRatio) const;

    static ZWInputMapping CreateDefaultInputMapping();

private:
    void UpdateDirectionVectors();

private:
    glm::vec3 mPosition{ 0.0f, 0.0f, 5.0f };
    glm::vec3 mFront{ 0.0f, 0.0f, -1.0f };
    glm::vec3 mUp{ 0.0f, 1.0f, 0.0f };
    glm::vec3 mRight{ 1.0f, 0.0f, 0.0f };
    glm::vec3 mWorldUp{ 0.0f, 1.0f, 0.0f };
    float mYaw{ -90.0f };
    float mPitch{ 0.0f };
    float mMoveSpeed{ 5.0f };
    float mLookSensitivity{ 0.12f };
    float mZoomSensitivity{ 2.0f };
    float mFieldOfView{ 60.0f };
    float mNearPlane{ 0.1f };
    float mFarPlane{ 1000.0f };
};

}
