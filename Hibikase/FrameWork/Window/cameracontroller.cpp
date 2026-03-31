#include <Window\cameracontroller.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace HApp
{

ZWCameraController::ZWCameraController()
{
    UpdateDirectionVectors();
}

void ZWCameraController::Update(const ZWInputSystem& inputSystem, const ZWInputMapping& inputMapping, float deltaTime)
{
    const float frameMoveSpeed = std::max(0.0f, deltaTime) * mMoveSpeed;

    if (inputSystem.IsActionDown("MoveForward", inputMapping))
    {
        mPosition += mFront * frameMoveSpeed;
    }

    if (inputSystem.IsActionDown("MoveBackward", inputMapping))
    {
        mPosition -= mFront * frameMoveSpeed;
    }

    if (inputSystem.IsActionDown("MoveLeft", inputMapping))
    {
        mPosition -= mRight * frameMoveSpeed;
    }

    if (inputSystem.IsActionDown("MoveRight", inputMapping))
    {
        mPosition += mRight * frameMoveSpeed;
    }

    const bool canRotate = inputSystem.IsCursorLocked() || inputSystem.IsActionDown("LookAround", inputMapping);
    if (canRotate)
    {
        mYaw += static_cast<float>(inputSystem.GetMouseDeltaX()) * mLookSensitivity;
        mPitch -= static_cast<float>(inputSystem.GetMouseDeltaY()) * mLookSensitivity;
        mPitch = std::clamp(mPitch, -89.0f, 89.0f);
        UpdateDirectionVectors();
    }

    if (std::abs(inputSystem.GetScrollDeltaY()) > 0.0)
    {
        mFieldOfView -= static_cast<float>(inputSystem.GetScrollDeltaY()) * mZoomSensitivity;
        mFieldOfView = std::clamp(mFieldOfView, 20.0f, 90.0f);
    }
}

void ZWCameraController::SetPosition(const glm::vec3& position)
{
    mPosition = position;
}

void ZWCameraController::SetRotation(float yaw, float pitch)
{
    mYaw = yaw;
    mPitch = std::clamp(pitch, -89.0f, 89.0f);
    UpdateDirectionVectors();
}

void ZWCameraController::SetProjection(float fieldOfView, float nearPlane, float farPlane)
{
    mFieldOfView = std::clamp(fieldOfView, 20.0f, 90.0f);
    mNearPlane = std::max(0.001f, nearPlane);
    mFarPlane = std::max(mNearPlane + 0.001f, farPlane);
}

void ZWCameraController::SetMoveSpeed(float moveSpeed)
{
    mMoveSpeed = std::max(0.0f, moveSpeed);
}

void ZWCameraController::SetLookSensitivity(float lookSensitivity)
{
    mLookSensitivity = std::max(0.001f, lookSensitivity);
}

void ZWCameraController::SetZoomSensitivity(float zoomSensitivity)
{
    mZoomSensitivity = std::max(0.001f, zoomSensitivity);
}

const glm::vec3& ZWCameraController::GetPosition() const
{
    return mPosition;
}

const glm::vec3& ZWCameraController::GetFrontVector() const
{
    return mFront;
}

float ZWCameraController::GetYaw() const
{
    return mYaw;
}

float ZWCameraController::GetPitch() const
{
    return mPitch;
}

float ZWCameraController::GetFieldOfView() const
{
    return mFieldOfView;
}

glm::mat4 ZWCameraController::BuildViewMatrix() const
{
    return glm::lookAt(mPosition, mPosition + mFront, mUp);
}

glm::mat4 ZWCameraController::BuildProjectionMatrix(float aspectRatio) const
{
    const float safeAspectRatio = aspectRatio > 0.0f ? aspectRatio : 1.0f;
    return glm::perspectiveRH_ZO(glm::radians(mFieldOfView), safeAspectRatio, mNearPlane, mFarPlane);
}

ZWInputMapping ZWCameraController::CreateDefaultInputMapping()
{
    ZWInputMapping inputMapping;
    inputMapping.SetDefaultMovementBindings();
    return inputMapping;
}

void ZWCameraController::UpdateDirectionVectors()
{
    const float yawRadians = glm::radians(mYaw);
    const float pitchRadians = glm::radians(mPitch);

    glm::vec3 front;
    front.x = std::cos(yawRadians) * std::cos(pitchRadians);
    front.y = std::sin(pitchRadians);
    front.z = std::sin(yawRadians) * std::cos(pitchRadians);

    mFront = glm::normalize(front);
    mRight = glm::normalize(glm::cross(mFront, mWorldUp));
    mUp = glm::normalize(glm::cross(mRight, mFront));
}

}
