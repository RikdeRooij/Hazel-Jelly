#include "Player.h"
#include "Hazel/Renderer/Renderer2D.h"
#include "Hazel/Core/Input.h"
#include <windows.h>
#include <playsoundapi.h>
#include <mmsystem.h>
#include "../vendor/tinyxml2/tinyxml2.h"
#include "TextureAtlas.h"
#pragma comment(lib, "winmm.lib")

using namespace Jelly;

//#define TEST 1

#define PM_SCALE 1.0f

#define MOVE_SIDE (3.0f * PM_SCALE) // Horizontal (left-right) movement power
#define MOVE_SIDE_LIMIT (4.0f * PM_SCALE) // Horizontal (left-right) movement limit

#define MOVE_JUMP_UP (7.0f * PM_SCALE) // jump power
#define MOVE_JUMP_TIME 200 // min time in milliseconds between jumps

#define MOVE_WALLJUMP_UP (4.0f * PM_SCALE) // wall-jump up power
#define MOVE_WALLJUMP_SIDE (4.0f * PM_SCALE) // wall-jump side (away from wall) power

#define JUMP_NOXMOVE_TIME 50.0f // miliseconds x movement is limited after jump

#define MOVE_FALLOFF 1.0f // speed at which move-power starts reducing
#define MOVE_FALLOFF_POWER 1.0f // scales reduction power on movement at high speed


#define MOVE_AIR 0.25f // amount of movement when not grounded
#define MOVE_AIR_MOMENTUM 3.0f // momentum (speed) needed to move when fully airborne (reducing move power when less)
#define MOVE_AIR_COLLDING 0.5f // amount of movement when not grounded but still touching a wall or ceiling


#define MOVE_DAMPING 3.0f // damping when grounded

#define GROUND_NORMAL 0.7f // max absolute normal.x to be considered ground. (acos(0.7) * (180/PI) = 45.6 degrees)


Player::Player(float x, float y, float size, float scale, PhysicsManager* physicsMgr)
{
    dontDestroy = true;

    b2BodyDef bodyDef;
    bodyDef.type = b2BodyType::b2_dynamicBody;
    bodyDef.position.Set(x*UNRATIO, y*UNRATIO);

    b2PolygonShape shape;
    shape.SetAsBox(size*0.5f*UNRATIO, size*0.5f*UNRATIO);

    b2FixtureDef fixtureDef = FixtureData(1.0f, 0.2f, 0.45f, "Player");
    fixtureDef.shape = &shape;

    b2Body* physBody = physicsMgr->GetPhysicsWorld()->CreateBody(&bodyDef);
    b2BodyUserData data;
    data.pointer = reinterpret_cast<uintptr_t>(this);
    physBody->SetUserData(data);
    physBody->CreateFixture(&fixtureDef);
    physicsMgr->AddPhysicsObject(physBody);

    physBody->SetFixedRotation(true);
    //physBody->SetLinearDamping(MOVE_DAMPING);
    physBody->SetLinearDamping(0);

    time = 0;
    lastJumpTime = 0;
    lastInsideTime = 0;

    m_body = physBody;

    //dontDestroy = false;
    dontDraw = false;

    width = size + 0.1f;// tsize.x * scale;
    height = size + 0.1f;//tsize.y * scale;

    this->tex = Hazel::Texture2D::Create("assets/Jelly2.png");
    glm::vec2 tsize = { tex.Get()->GetWidth(), tex.Get()->GetHeight() };
    float tdx = (size + 0.08f) / tsize.x;
    float tdy = (size + 0.08f) / tsize.y;
    float td = max(tdx, tdy);
    width = tsize.x * td;// +0.05f;
    height = tsize.y * td;// +0.05f;

    posx = m_body->GetPosition().x * RATIO;
    posy = m_body->GetPosition().y * RATIO;
    angle = toDegrees(m_body->GetAngle());
    origin = { .5f, .5f };
    clr = { 1, 1, 1, 1 };


    // Init here
    m_Particle.ColorBegin = { 0.0f, 0.9f, 0.0f, 1.0f };
    m_Particle.ColorEnd = { 0.0f, 0.8f, 0.0f, 0.1f };
    m_Particle.Texture = Hazel::Texture2D::Create("assets/particle.png");
    m_Particle.SizeBegin = 0.25f, m_Particle.SizeVariation = 0.2f, m_Particle.SizeEnd = 0.0f;
    m_Particle.LifeTime = 0.60f;
    m_Particle.Velocity = { 0.1f, 0.1f };
    m_Particle.VelocityVariation = { 2.5f, 2.5f };
    m_Particle.Position = { 0.0f, 0.0f };

    textureAtlas = TextureAtlas("assets/jelly_anim.xml");
}

Player::~Player()
{

}

void Player::Update(float dt)
{
    time += dt * 1000;
    animtime += dt;


    //b2Vec2 pos = getBody()->GetPosition();
    prev_vel = vel;
    vel = GetBody()->GetLinearVelocity();

    bool was_grounded = grounded;
    bool was_wallLeft = wallLeft;
    bool was_wallRight = wallRight;

    UpdateCollisions(vel);

    if (inside)
    {
        lastInsideTime = time;
        lastInsideY = posy;
    }

    //getBody()->SetLinearVelocity(vel);
    speed = vel.Length();


    if (grounded && !was_grounded)
    {
        if (prev_vel.y < -4.f)
        {
            DBG_OUTPUT("land:   %.3f   %.3f", vel.y, prev_vel.y);
            lastLandTime = time;
        }
    }

    bool klr = (key_left || key_right);
    bool wall_squish = klr && (wallLeft || wallRight || was_wallLeft || was_wallRight);
    anim_squish = Interpolate::Linear(anim_squish, wall_squish, dt * 10);


    if (Hazel::Input::BeginKeyPress(Hazel::Key::X))
        Explode();

    if (Hazel::Input::BeginKeyPress(Hazel::Key::K))
        Die();


    float damping = !inside && grounded ? MOVE_DAMPING : 0;
    GetBody()->SetLinearDamping(damping);



    if (Hazel::Input::BeginKeyPress(Hazel::Key::Slash))
    {
        debugMode = !debugMode;
        //GetBody()->SetType(debugMode ? b2_kinematicBody : b2_dynamicBody);
        GetBody()->SetGravityScale(debugMode ? 0.f : 1.f);
    }

    if (debugMode)
    {
        b2Vec2 impulse = b2Vec2(0, 0);

        if (Hazel::Input::IsKeyPressed(Hazel::Key::Left))
            impulse.x += -1;
        if (Hazel::Input::IsKeyPressed(Hazel::Key::Right))
            impulse.x += 1;
        if (Hazel::Input::IsKeyPressed(Hazel::Key::Up))
            impulse.y += 1;
        if (Hazel::Input::IsKeyPressed(Hazel::Key::Down))
            impulse.y += -1;

        GetBody()->SetLinearVelocity(impulse);
    }
    else
    {
        UpdateMove(vel);

        if (dead && GetBody())
        {
            this->height = -std::abs(height);
            GetBody()->SetLinearVelocity(b2Vec2(0, 0));
            GetBody()->SetAwake(false);
        }
    }

    GameObject::Update(dt);
}

bool IsKeyPressed(Hazel::KeyCode key, Hazel::KeyCode alt)
{
    return Hazel::Input::IsKeyPressed(key)
        || Hazel::Input::IsKeyPressed(alt);
}

void Player::UpdateCollisions(b2Vec2& vel)
{
    auto go_pos = GetPosition();

    grounded = false;
    wallLeft = false;
    wallRight = false;
    ceiling = false;
    inside = false;

#if DEBUG
    contacts.clear();
#endif

    for (b2ContactEdge* ce = GetBody()->GetContactList(); ce; ce = ce->next)
    {
        b2Contact* c = ce->contact;

        //DBG_WRITE("%s  ##  %s", ((char*)c->GetFixtureA()->GetUserData()), ((char*)c->GetFixtureA()->GetUserData()));
        //c->GetFixtureA()
        //if (c->GetFixtureA() == (*enemyBody)[i]->GetFixtureList())

        b2Manifold* manifold = c->GetManifold();
        if (manifold->pointCount < 0)
            continue;

        b2WorldManifold worldManifold;
        c->GetWorldManifold(&worldManifold);


        glm::vec2 avgPos(0, 0);
        glm::vec2 ctrDirVec(0, 0);

        if (manifold->pointCount > 0)
        {
            for (int i = 0; i < manifold->pointCount; i++)
            {
                glm::vec2 sfPos = { worldManifold.points[i].x * RATIO, worldManifold.points[i].y * RATIO };
                avgPos += sfPos;
            }
            avgPos = glm::vec2(avgPos.x / (float)manifold->pointCount, avgPos.y / (float)manifold->pointCount);
            ctrDirVec = avgPos - go_pos;
        }
        else
        {
            avgPos = go_pos;
            ctrDirVec = glm::vec2(0, 0);
        }

        b2Vec2 wmNormal = worldManifold.normal;
        auto normal = b2Vec2(abs(wmNormal.x) * ctrDirVec.x,
                             abs(wmNormal.y) * ctrDirVec.y);
        normal.Normalize();

        bool isGround = (normal.y < 0 && abs(normal.x) < GROUND_NORMAL);
        bool isLeft = normal.x <= -GROUND_NORMAL;
        bool isRight = normal.x >= GROUND_NORMAL;
        bool isUp = (normal.y > 0 && abs(normal.x) < GROUND_NORMAL);

        bool isInside = manifold->pointCount > 0 &&
            (glm::length(ctrDirVec) < 0.1f ||
            (abs(ctrDirVec.x) < 0.2f && abs(ctrDirVec.y) < 0.19f));

        if (isInside)
            inside = true;

        {
            if (isGround && !isInside)
                grounded = true;
            else
            {
                if (isLeft)
                    wallLeft = true;
                if (isRight)
                    wallRight = true;
                if (isUp && !isInside)
                    ceiling = true;
            }
        }

#if DEBUG
        ContactData cd = ContactData(avgPos, { normal.x, normal.y }, isGround, isUp);
        contacts.push_back(cd);
#endif

        vel = clipVector(vel, normal, 0.33f);
    }
}

void Player::UpdateMove(b2Vec2& vel)
{
    float mass = GetBody()->GetMass();

    float jumpDeltaTime = abs(time - lastJumpTime);

    bool airborne = inside ||
        (!grounded && !wallLeft && !wallRight && !ceiling);

    float move_pwr_scale = grounded ? 1.0f :
        fmax(airborne ? 0 : MOVE_AIR_COLLDING,
             fmin(pow3(fmin(1.0f, speed / MOVE_AIR_MOMENTUM)) * MOVE_AIR, MOVE_AIR));

    float move_falloff = 1.0f + fmax(speed - MOVE_FALLOFF, 0.0f) * MOVE_FALLOFF_POWER;
    //move_falloff = 1.0f;

    float power = (mass / move_falloff) * move_pwr_scale;

    // _JUMP vel.y:  0.0 | 3.4 | 4.2
    bool allowJump = //!inside &&  
        vel.y < 4.5f &&
        (grounded || wallLeft || wallRight); // || !ceiling)

    bool allowUpJump = allowJump && !inside;
    if (!allowUpJump && inside && grounded && !wallLeft && !wallRight && abs(vel.y) < 0.01f)
        allowUpJump = true;

    if (allowJump && (jumpDeltaTime > MOVE_JUMP_TIME))
    {
        if (IsKeyPressed(Hazel::Key::Up, Hazel::Key::W))
        {
            //if(grounded) DBG_OUTPUT("_JUMP %.1f", vel.y);

            float velup = clamp01(vel.y); // 1 when going up
            float veldn = clamp01(-vel.y); // 1 when going down

            float opposite = (1.0f - veldn * 0.8f); // smaller when going down

            if (allowUpJump && grounded && vel.y < MOVE_JUMP_UP * 0.85f)
            {
                float extend = (1.0f + velup * 0.2f); // larger when going up
                float limitjump = (lastInsideY <= posy ? clamp01(abs(time - lastInsideTime) / 200.0f) : 1.0f);
                // *clamp01(jumpDeltaTime / (MOVE_JUMP_TIME * 2));
                float jumpvel = max(vel.y, MOVE_JUMP_UP * opposite * extend * limitjump);
                if (jumpvel - vel.y > MOVE_JUMP_UP * 0.1f)
                {
                    Jump(vel.x, jumpvel);
                    lastJumpTime = time;
                    if (velup < 0.5f) PlayJumpSound(0);
                    else PlayJumpSound(1);
                }
            }
            else
            {
                float reduce = (1.0f - velup * 0.4f); // smaller when going up

                if (wallLeft)
                {
                    auto force = GetBody()->GetForce();
                    force.x *= 0.3f;
                    GetBody()->SetForce(force);

                    Jump(MOVE_WALLJUMP_SIDE * reduce, MOVE_WALLJUMP_UP * opposite);
                    lastJumpTime = time;
                    if (veldn < 0.5f) PlayJumpSound(0);
                    else PlayJumpSound(2);
                }
                if (wallRight)
                {
                    auto force = GetBody()->GetForce();
                    force.x *= 0.3f;
                    GetBody()->SetForce(force);

                    Jump(-MOVE_WALLJUMP_SIDE * reduce, MOVE_WALLJUMP_UP * opposite);
                    lastJumpTime = time;
                    if (veldn < 0.5f) PlayJumpSound(0);
                    else PlayJumpSound(2);
                }
            }
        }
    }

    bool keyleft = IsKeyPressed(Hazel::Key::Left, Hazel::Key::A);
    if (keyleft)
    {
#if TEST
        float limit = clamp01(MOVE_SIDE_LIMIT + vel.x) * clamp01(jumpDeltaTime / JUMP_NOXMOVE_TIME);
        moveX(-power * limit * MOVE_SIDE);
#else
        float opposite = !grounded ? 1.0f : 0.05f + pow2(clamp01(-(vel.x / MOVE_SIDE_LIMIT)));
        float limit = clamp01(MOVE_SIDE + vel.x) * clamp01(jumpDeltaTime / JUMP_NOXMOVE_TIME) * opposite;
        MoveX(-fmin(power * limit * MOVE_SIDE, MOVE_SIDE_LIMIT));
#endif
        this->width = std::abs(width);
    }

    bool keyright = IsKeyPressed(Hazel::Key::Right, Hazel::Key::D);
    if (keyright)
    {
#if TEST
        float limit = clamp01(MOVE_SIDE_LIMIT - vel.x) * clamp01(jumpDeltaTime / JUMP_NOXMOVE_TIME);
        moveX(power * limit * MOVE_SIDE);
#else
        float opposite = !grounded ? 1.0f : 0.05f + pow2(clamp01((vel.x / MOVE_SIDE_LIMIT)));
        float limit = clamp01(MOVE_SIDE - vel.x) * clamp01(jumpDeltaTime / JUMP_NOXMOVE_TIME) * opposite;
        MoveX(fmin(power * limit * MOVE_SIDE, MOVE_SIDE_LIMIT));
#endif
        this->width = -std::abs(width);
    }

    if (IsKeyPressed(Hazel::Key::Down, Hazel::Key::S))
    {
        Move(0.f, -1.f);
    }

    //if (!jumpanim)
    //{
    //    if (key_left != keyleft) animtime = 0;
    //    if (key_right != keyright) animtime = 0;
    //}
    key_left = keyleft;
    key_right = keyright;
}

void Player::MoveX(float power) const
{
    if (dead)
        return;

#if TEST
    b2Vec2 impulse = b2Vec2(power, 0);
    b2Vec2 impulsePoint = GetBody()->GetPosition();
    GetBody()->ApplyLinearImpulse(impulse, impulsePoint, true);
#else
    //DBG_OUTPUT("moveX:: %.3f", (power));
    GetBody()->ApplyForceToCenter({ power * 100, 0 }, true);
#endif
}

void Player::Jump(float x, float power)
{
    if (dead)
        return;

    //if (!jumpanim)
    {
        jumpanim = true;
        animtime = 0;
    }

    //PlayJumpSound();

    b2Vec2 impulse = b2Vec2(x, power);
    GetBody()->SetLinearVelocity(impulse);
}

void Player::Move(float dx, float dy) const
{
    if (dead)
        return;

    b2Vec2 vel = GetBody()->GetLinearVelocity();
    float power = GetBody()->GetMass() * (1.0f / fmax(vel.Length(), 1.0f));

    b2Vec2 impulse = b2Vec2(dx * power, dy * power);
    b2Vec2 impulsePoint = GetBody()->GetPosition();
    GetBody()->ApplyLinearImpulse(impulse, impulsePoint, true);
}

void Player::Explode()
{
    m_Particle.Position = { posx , posy };
    ParticleSystem::S_Emit(100, m_Particle);
}

void Player::Die()
{
    if (dead)
        return;

    GetBody()->SetAwake(false);

    SetColor({ 1,1,1,0.1f });

    Explode();

    PlaySoundA("assets/Sounds/laser6.wav", nullptr, SND_FILENAME | SND_ASYNC);

    dead = true;
}

void Player::PlayJumpSound(int i) const
{
    PlaySoundA(format("assets/Sounds/jump%d.wav", (i + 1)).c_str(), nullptr, SND_FILENAME | SND_ASYNC);
}

void Player::Draw(int layer)
{
    if (dontDraw)
        return;
    //GameObject::Draw(layer);

    if (this->draw_layer != layer)
        return;

    float px = posx + abs(width) * (0.5f - origin.x);
    float py = posy + height * (0.5f - origin.y);


    auto z = -0.99f + (static_cast<float>(type) / static_cast<float>(Objects::MAX_COUNT) * 0.5f);
    z += (instanceID * 0.000001f);

    const float anim_speed_breathe = 3;

    const float anim_idle_speed = 5;
    const float anim_move_speed = anim_idle_speed;


    float anim_land = clamp01(abs(lastLandTime - time) * 0.005f);
    float hm = anim_land * 0.3f + 0.7f;
    py -= height * (1-hm) * .25f;
    float wm = 1 +((1- anim_land) * 0.15f);

    float s = Interpolate::Hermite(0, 1, TextureAtlas::AnimTime((animtime + 1.5f) * anim_speed_breathe, 3.0f, true) / 3.0f);
    const float breathe = 0.04f;
    float sw = (s * breathe);// *1.5f;
    float sh = ((0.5f + (0.5f - s)) * breathe);
    py += sh * .5f;
    px += sign(width) * 0.04f; // perspective image

    if (textureAtlas.Valid() && jumpanim)
    {
        const float animspeed = 7.0f;// 4.5f;
        const uint acnt = 3;

        //if (animtime * animspeed >= ((acnt - 1) * 2 - 0.1f))
        if (animtime * animspeed >= (acnt - 0.1f))
            jumpanim = false;
        auto  texRect = textureAtlas.AnimationRect(animtime * animspeed + 1, 6, acnt, false);

        Hazel::Renderer2D::DrawRotatedQuad({ px, py, z }, { width * wm + sign(width) * sw, height * hm + sh }, angle,
                                           textureAtlas.GetTextureRef().Get(), 
                                           { texRect.z, texRect.w }, { texRect.x, texRect.y }, clr);
    }
    else if (textureAtlas.Valid())
    {
        const float minxspd = 0.9f;
        bool klr = (key_left || key_right) && (speed > minxspd && abs(vel.x) > minxspd && abs(prev_vel.x) > minxspd && (!wallLeft && !wallRight));
        float animspeed = klr ? anim_move_speed : anim_idle_speed;

        auto texRect = textureAtlas.AnimationRect(animtime * animspeed, klr ? 5 : 0, klr ? -3 : 3, true);
        //texRect = textureAtlas.GetRect(6);

        float fangle = angle;
        //fangle = angle - width * anim_squish * 4;

        const float amd = 0.1f;
        wm = wm * (clamp01(1 - anim_squish) * amd + (1-amd));
        hm = hm * (clamp01(anim_squish) * amd + 1.0f);
        px -= width * clamp01(anim_squish) * amd * 0.25f;


        Hazel::Renderer2D::DrawRotatedQuad({ px, py, z }, { width * wm + sign(width) * sw, height * hm + sh }, fangle,
                                           textureAtlas.GetTextureRef().Get(),
                                           { texRect.z, texRect.w }, { texRect.x, texRect.y }, clr);
    }
    else
    {
        if (tex.Has())
            Hazel::Renderer2D::DrawRotatedQuad({ px, py, z }, { width * wm + sign(width) * sw, height * hm + sh }, angle,
                                               tex.Get(), tex_tiling, tex_offset, clr);
        else
            Hazel::Renderer2D::DrawRotatedQuad({ px, py, z }, { width * wm + sign(width) * sw, height * hm + sh }, angle, clr);
    }
}
