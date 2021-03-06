#pragma once

//#define PM_SCALE 2.0f

#include "PhysicsManager.h"
#include "Character.h"
#include "Weapon.h"

namespace Jelly
{
    class Player :
        public Character
    {
    public:
        static Player* instance;

        Player(b2Body* bd, TextureAtlas textureRef, float w, float h, float scale);
        ~Player();

        Input UpdateInput() override;

        virtual void Die() override;
        virtual void OnHit(GameObject* by) override;


        virtual bool OnCollision(b2Vec2 normal, GameObject* other) override;
        

        virtual void Draw(int layer) const override;


        virtual void Update(float dt) override;


        virtual void Delete() override;

        float aimangle = 0;
        Weapon weapon;
    };
}
