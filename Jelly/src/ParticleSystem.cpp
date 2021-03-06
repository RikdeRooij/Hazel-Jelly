#include "ParticleSystem.h"

#include "Globals.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/compatibility.hpp>
#include "Hazel/Renderer/Renderer2D.h"
#include "RaysCastCallback.h"

using namespace Jelly;

ParticleSystem* ParticleSystem::instance = nullptr;

ParticleSystem::ParticleSystem()
{
    this->instance = this;
    m_ParticlePool.resize(1000);
}

ParticleSystem::~ParticleSystem()
{
    this->instance = nullptr;
}

void ParticleSystem::OnUpdate(float ts)
{
    m_Count = 0;
    for (auto& particle : m_ParticlePool)
    {
        if (!particle.Active)
            continue;

        if (particle.LifeRemaining <= 0.0f)
        {
            particle.Active = false;
            continue;
        }

        m_Count++;
        particle.LifeRemaining -= ts;

        RaysCastCallback callback = RaysCastCallback::RayCastVec({ particle.Position.x, particle.Position.y },
                                                                 { particle.Velocity.x * (float)ts, particle.Velocity.y * (float)ts });
        if (callback.m_hit && callback.m_fraction > 0)
        {
            particle.Velocity = reflectVector(particle.Velocity, { callback.m_normal.x , callback.m_normal.y });
        }

        particle.Position += particle.Velocity * (float)ts;
        particle.Rotation += 0.01f * ts;

        auto a = floor((1 - (particle.LifeRemaining / particle.LifeTime)) * particle.AnimationCount);
        particle.AnimationIndex = clamp(static_cast<int>(a), 0, particle.AnimationCount-1);
    }
}

void ParticleSystem::OnRender()
{
    int i = m_Count;
    for (auto& particle : m_ParticlePool)
    {
        if (!particle.Active)
            continue;

        // Fade away particles
        float life = particle.LifeRemaining / particle.LifeTime;
        glm::vec4 color = glm::lerp(particle.ColorEnd, particle.ColorBegin, life);
        //color.a = color.a * life;

        float size = glm::lerp(particle.SizeEnd, particle.SizeBegin, life);

        float z = 0.99f - (--i) * 0.001f;
        
        //glm::mat4 transform = glm::translate(glm::mat4(1.0f), { particle.Position.x, particle.Position.y, 0.0f })
        //    * glm::rotate(glm::mat4(1.0f), particle.Rotation, { 0.0f, 0.0f, 1.0f })
        //    * glm::scale(glm::mat4(1.0f), { size, size, 1.0f });

        if (particle.Animation.Valid())
        {
            auto texrect = particle.Animation.GetRect(particle.AnimationIndex);
            Hazel::Renderer2D::DrawRotatedQuad({ particle.Position.x, particle.Position.y, z }, { size,size }, particle.Rotation,
                                               particle.Animation.GetTextureRef().Get(), { texrect.z, texrect.w }, { texrect.x, texrect.y }, color);
        }
        else if (particle.Texture.Has())
        {
            Hazel::Renderer2D::DrawRotatedQuad({ particle.Position.x, particle.Position.y, z }, { size,size }, particle.Rotation,
                                               particle.Texture.Get(), { 1.f,1.f }, color);
        }
        else
        {
            Hazel::Renderer2D::DrawRotatedQuad({ particle.Position.x, particle.Position.y, z }, { size,size }, particle.Rotation, color);
        }
    }
}

void ParticleSystem::Clear()
{
    for (auto& particle : m_ParticlePool)
    {
        if (!particle.Active)
            continue;
        particle.Active = false;
    }
}

void ParticleSystem::Emit(const ParticleProps& particleProps)
{
    Particle& particle = m_ParticlePool[m_PoolIndex];
    particle.Active = true;
    particle.Position = particleProps.Position;
    particle.Rotation = Random::Float() * 2.0f * glm::pi<float>();

    // Velocity
    particle.Velocity = particleProps.Velocity;
    particle.Velocity.x += particleProps.VelocityVariation.x * (Random::Float() - 0.5f);
    particle.Velocity.y += particleProps.VelocityVariation.y * (Random::Float() - 0.5f);

    // Color
    particle.ColorBegin = particleProps.ColorBegin;
    particle.ColorEnd = particleProps.ColorEnd;
    particle.Texture = particleProps.Texture;
    particle.Animation = particleProps.Animation;
    particle.AnimationCount = particleProps.Animation.RectCount();

    particle.LifeTime = particleProps.LifeTime;
    particle.LifeRemaining = particleProps.LifeTime;
    particle.SizeBegin = particleProps.SizeBegin + particleProps.SizeVariation * (Random::Float() - 0.5f);
    particle.SizeEnd = particleProps.SizeEnd;

    m_PoolIndex = --m_PoolIndex % m_ParticlePool.size();
}
