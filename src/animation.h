#ifndef ANIMATION_H
#define ANIMATION_H
#include "tools.h"
#include "platform.h"
#include "shader.h"
#include "texture.h"

static Shader anim_shader;

//NOTE(ilias): lets implement frozen transitions!

typedef struct Joint
{
    u32 index;
    String name;
    String sid;
    u32 num_of_children;
    u32 parent_id;
    mat4 animated_transform; //joint transform
    mat4 local_bind_transform;
    mat4 inv_bind_transform;
}Joint;
typedef struct AnimatedVertex
{
    vec3 position;
    vec3 normal;
    vec2 tex_coord;
    ivec3 joint_ids;
    vec3 weights;
}AnimatedVertex;

typedef struct MeshData{
    vec3 *positions; 
    vec3 *normals; 
    vec2 *tex_coords; 
    Vertex *verts; //just for rendering
    i32 vertex_count;
    i32 *joint_ids; 
    f32 *weights; 
    u32 size;

    mat4 *transforms;
    i32 transforms_count;
    mat4 bind_shape_matrix;

    Joint root;
    AnimatedVertex *vertices;

    Joint *joints;
    u32 joint_count;

} MeshData;

static Joint 
joint_sid(u32 index, String name,String sid, mat4 local_bind_transform)
{
    Joint j;
    j.index = index;
    j.name = name;
    
    j.sid = sid;
    j.local_bind_transform = local_bind_transform;
    //this could just be a size in j.children 
    j.num_of_children = 0;
    //j.children = NULL;
    //j.children = (Joint*)malloc(sizeof(Joint) * 10);
    j.inv_bind_transform = m4d(1.f);
    j.animated_transform = m4d(1.f);
    
    return j;
}

static Joint 
joint(u32 index, String name, mat4 local_bind_transform)
{
    Joint j;
    
    j.index = index;
    j.name = name;
    j.local_bind_transform = local_bind_transform;
    //j.inv_bind_transform = {0};
    //j.animated_transform = {0};
    
    return j;
}

//represents the position and rotation of a joint in an animation frame (wrt parent)
typedef struct JointTransform
{
    vec3 position;
    Quaternion rotation;
    mat4 transform; //this is not mandatory
}JointTransform;

typedef struct JointKeyFrame
{
    f32 timestamp;
    u32 joint_index;
    JointTransform transform;
}JointKeyFrame;

typedef struct JointAnimation
{
    JointKeyFrame *keyframes;
    u32 keyframe_count;
    f32 length;
}JointAnimation;

typedef struct Animation
{
    JointAnimation *joint_animations;
    u32 joint_anims_count;
    f32 length;
    f32 playback_rate;
}Animation;



typedef struct AnimatedModel
{
    //skin
    GLuint vao;
    mat4 * transforms;
    Texture * diff_tex;
    //Texture * spec_tex;
    
    //skeleton
    Joint root;
    u32 joint_count;
    Joint *joints;
    mat4 bind_shape_matrix;
    u32 vertices_count;
    
}AnimatedModel;


typedef struct Animator
{
    AnimatedModel model;
    Animation* anim;
    f32 animation_time;

    JointKeyFrame *prev_pose;
    f32 blend_percentage; //in [0,1], tells us how much of prev_pose we should blend
    f32 blend_time; //time the (linear) blending should take place =1?
}Animator;

//is this correct? sure hope so..
typedef Animator AnimatorComponent;



static JointTransform 
joint_transform(vec3 position, Quaternion rotation)
{
    JointTransform res;
    res.position = position;
    res.rotation = rotation;
    return res;
}

static mat4
get_joint_transform_matrix(JointTransform j)
{
    mat4 res;
    res = mul_mat4(translate_mat4(j.position), quat_to_mat4(j.rotation));
    return res;
}

static JointTransform
interpolate_joint_transforms(JointTransform l, JointTransform r, f32 time)
{
    JointTransform res = {0};
    vec3 pos = lerp_vec3(l.position,r.position, time);
    Quaternion q = nlerp(l.rotation, r.rotation, time); //maybe we need slerp??
    res.position = pos;
    res.rotation = q;
    return res;
}
static void 
increase_animation_time(Animator* animator)
{
    assert(animator);
    animator->animation_time += global_platform.dt * animator->anim->playback_rate * 1.5f; //this should be the Δt from global platform but its bugged rn
    if (animator->animation_time > animator->anim->length)
        animator->animation_time -= animator->anim->length;
    //NOTE(ilias): this is in case playback rate is negative
    if (animator->animation_time < 0.f)
      animator->animation_time += animator->anim->length;
    //TODO(ilias): check da math
    animator->blend_percentage -= (1.f/(animator->blend_time)) *global_platform.dt;
    if (animator->blend_percentage < 0.f)
      animator->blend_percentage = 0.f;

}

//mat4 animated_joint_transform = concat_local_transforms(joints, local_transforms, index); 
static mat4
concat_local_transforms(Joint *joints, mat4 *local_transforms, u32 index)
{
    //root has parent id == its index
    if (index == joints[index].parent_id)
        return local_transforms[index];
    return mul_mat4(concat_local_transforms(joints, local_transforms, joints[index].parent_id), local_transforms[index]); 
}


static void
calc_animated_transform(Animator *animator, Joint *joints, mat4 *local_transforms, u32 index)
{
    //here we get the animated joint transform meaning the world pos of the joint in the animation
    mat4 animated_joint_transform = concat_local_transforms(joints, local_transforms, index); 
    joints[index].animated_transform = mul_mat4(animated_joint_transform, joints[index].inv_bind_transform);
}

char joint_transforms[21] = "joint_transforms[00]";
char joint_transforms_one[20] = "joint_transforms[0]";
static void 
set_joint_transform_uniforms(AnimatedModel* model,Shader* s, Joint *j)
{
    if (j->index >= 10){
        joint_transforms[17] = '0' + (j->index/10);
        joint_transforms[18] = '0' + (j->index -(((int)(j->index/10)) * 10));
        setMat4fv(s, joint_transforms, (f32*)j->animated_transform.elements);
    }else
    {
        joint_transforms_one[17] = '0' + (j->index);
        setMat4fv(s, joint_transforms_one, (f32*)j->animated_transform.elements);
    }
}




static JointKeyFrame* 
get_previous_and_next_keyframes(Animator* animator, i32 joint_animation_index)
{
    JointKeyFrame frames[2];
    JointKeyFrame* all_frames = animator->anim->joint_animations[joint_animation_index].keyframes;
    JointKeyFrame prev = all_frames[0];
    JointKeyFrame next = all_frames[0];
    f32 animation_time = animator->animation_time;
    for (i32 i = 1; i < animator->anim->joint_animations[joint_animation_index].keyframe_count; ++i)
    {
        next = all_frames[i];
        if (next.timestamp > animator->animation_time)
            break;
        prev = all_frames[i];
    }
    frames[0] = prev;
    frames[1] = next;
    return (frames);
}

static f32 calc_progress(Animator* animator, JointKeyFrame prev, JointKeyFrame next)
{
    f32 total_time = next.timestamp - prev.timestamp;
    f32 current_time = animator->animation_time - prev.timestamp;
    if (current_time/total_time < 0.04)return 0;
    return current_time / total_time;
}

JointKeyFrame interpolate_poses(JointKeyFrame prev, JointKeyFrame next, f32 x)
{
    JointKeyFrame res;
    
    res.transform.position = lerp_vec3(prev.transform.position, next.transform.position, x);
    res.transform.rotation = nlerp(prev.transform.rotation, next.transform.rotation, x);
    res.joint_index = prev.joint_index;
    
    return res;
    
}

static JointKeyFrame calc_current_animation_pose(Animator* animator, u32 joint_animation_index)
{
    JointKeyFrame* frames = get_previous_and_next_keyframes(animator, joint_animation_index);
    f32 x = calc_progress(animator, frames[0],frames[1]);
    return interpolate_poses(frames[0],frames[1], x);
}


static void
update_animator(Animator* animator)
{
    if (animator->anim == NULL)return;
    increase_animation_time(animator);
      //this is the array holding the animated local bind transforms for each joint,
    //if there is no animation in a certain joint its simply m4d(1.f)
    mat4 *local_animated_transforms= (mat4*)arena_alloc(&global_platform.frame_storage, sizeof(mat4) * animator->model.joint_count);
    for (i32 i = 0; i < animator->model.joint_count; ++i)
    {
        local_animated_transforms[i] = m4d(1.f);//mul_mat4(translate_mat4((vec3){0,0,0}), quat_om_angle((vec3){0,1,0}, 0));
    }

    //setting every prev joint pose to m4d(1.f)
    for (u32 i = 0; i < animator->model.joint_count; ++i)
    {
        JointKeyFrame current_pose = calc_current_animation_pose(animator, i); 
        if (animator->blend_percentage < 0.001f)
        {
          animator->prev_pose[current_pose.joint_index] = (JointKeyFrame){0};
          animator->prev_pose[current_pose.joint_index].transform.rotation = (Quaternion){0};//quat_from_angle(v3(0,1,0),0);
        }
    }
    //we put the INTERPOLATED local(wrt parent) animated transforms in the array
    for (u32 i = 0; i < animator->anim->joint_anims_count; ++i)
    {
        JointKeyFrame current_pose = calc_current_animation_pose(animator, i); 
        //JointKeyFrame current_pose = animator->anim->joint_animations[i].keyframes[((int)(global_platform.current_time * 24) % animator->anim->joint_animations[i].keyframe_count)];
        mat4 local_animated_transform = mul_mat4(translate_mat4(current_pose.transform.position), quat_to_mat4(current_pose.transform.rotation));
        local_animated_transforms[current_pose.joint_index] = local_animated_transform;
        if (animator->blend_percentage > 0.001f)
        {
          current_pose = interpolate_poses(current_pose,animator->prev_pose[current_pose.joint_index], animator->blend_percentage);
          mat4 local_animated_transform = mul_mat4(translate_mat4(current_pose.transform.position), quat_to_mat4(current_pose.transform.rotation));
          local_animated_transforms[current_pose.joint_index] = local_animated_transform;
        }else
          animator->prev_pose[current_pose.joint_index] = current_pose;
    }

    //now we recursively apply the pose to get the animated bind(wrt world) transform
    for (u32 i = 0; i < animator->model.joint_count;++i)
        calc_animated_transform(animator, animator->model.joints, local_animated_transforms, animator->model.joints[i].index);

    for (u32 i = 0; i < animator->model.joint_count; ++i)
        animator->model.joints[i].animated_transform = mul_mat4(animator->model.joints[i].animated_transform, animator->model.bind_shape_matrix);

}

static GLuint 
create_animated_model_vao(MeshData* data)
{
    GLuint vao;
    GLuint ebo;
    
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    //glGenBuffers(1,&ebo);
    //glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo); 
    //glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(data->indices_size), &data->indices, GL_STATIC_DRAW);
    
    //positions
    glEnableVertexAttribArray(0);
    
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    
    glBufferData(GL_ARRAY_BUFFER, sizeof(AnimatedModel) * data->vertex_count, &data->vertices[0], GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 11 + sizeof(int) * 3, (void*)(0));
    //normals
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 11 + sizeof(int) * 3, (void*) (sizeof(float) * 3));
    //tex_coords
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 11 + sizeof(int) * 3, (void*) (sizeof(float) * 6));
    //joints (max 3)
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 3, GL_INT,sizeof(float) * 11 + sizeof(int) * 3, (void*) (sizeof(float) * 8));
    //joint weights (max 3)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 11 + sizeof(int) * 3, (void*) (sizeof(float) * 8 + sizeof(int) * 3));
    
    glBindVertexArray(0);
    
    
    return vao;
}
static void 
render_animated_model_static(AnimatedModel* model, Shader* s, mat4 proj, mat4 view, vec3 translate, f32 y_dir)
{
    use_shader(s);
    
    setMat4fv(s, "projection_matrix", (GLfloat*)proj.elements);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,model->diff_tex->id);
    setInt(s, "diffuse_map", 1); //we should really make the texture manager global or something(per Scene?)... sigh
    //for(i32 i = 0; i < model->joint_count; ++i)
    //to start things off, everything is identity!
    view = mul_mat4(view, translate_mat4(translate));
    view = mul_mat4(view, quat_to_mat4(quat_from_angle(v3(0,1 * y_dir,0), (PI/2) * global_platform.current_time)));
#if 1
    {
        mat4 identity = m4d(1.f);
        setMat4fv(s, "joint_transforms[0]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[1]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[2]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[3]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[4]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[5]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[6]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[7]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[8]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[9]", (GLfloat*)identity.elements);
        //@memleak
        char *str = "joint_transforms[xx]";

        for (i32 i = 10; i < model->joint_count; ++i)
        {
            str[17] = '0' + (i/10);
            str[18] = '0' + (i -(((int)(i/10)) * 10));
            setMat4fv(s, str, (GLfloat*)identity.elements);
        }
    }
#endif
    setMat4fv(s, "view_matrix", (GLfloat*)view.elements);
    glUniform3f(glGetUniformLocation(s->ID, "light_direction"), 0.43,0.34,0.f); 

    glBindVertexArray(model->vao);
    glDrawArrays(GL_TRIANGLES,0, model->vertices_count);
    //glDrawArrays(GL_LINES,0, 20000);
    glBindVertexArray(0);
    
}


//this is so bad
static void 
render_animated_model(AnimatedModel* model, Shader* s, mat4 proj, mat4 view)
{
    use_shader(s);
    
    setMat4fv(s, "projection_matrix", (GLfloat*)proj.elements);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,model->diff_tex->id);
    setInt(s, "diffuse_map", 1); //we should really make the texture manager global or something(per Scene?)... sigh
    //for(i32 i = 0; i < model->joint_count; ++i)
    //to start things off, everything is identity!
#if 1
    {
        mat4 identity = m4d(1.f);
        setMat4fv(s, "joint_transforms[0]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[1]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[2]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[3]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[4]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[5]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[6]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[7]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[8]", (GLfloat*)identity.elements);
        setMat4fv(s, "joint_transforms[9]", (GLfloat*)identity.elements);
        //@memleak
        char *str = "joint_transforms[xx]";

        for (i32 i = 10; i < model->joint_count; ++i)
        {
            str[17] = '0' + (i/10);
            str[18] = '0' + (i -(((int)(i/10)) * 10));
            setMat4fv(s, str, (GLfloat*)identity.elements);
        }
    }
#endif

    for (u32 i = 0; i < model->joint_count; ++i)
        set_joint_transform_uniforms(model,s, &model->joints[i]);
    setMat4fv(s, "view_matrix", (GLfloat*)view.elements);
    glUniform3f(glGetUniformLocation(s->ID, "light_direction"), 0.43,0.34,0.f); 

    glBindVertexArray(model->vao);
    glDrawArrays(GL_TRIANGLES,0, model->vertices_count);
    //glDrawArrays(GL_LINES,0, 20000);
    glBindVertexArray(0);
    
}

static AnimatedModel
init_animated_model(Texture* diff, Joint root,MeshData* data)
{
    AnimatedModel model = {0};
    
    model.vao = create_animated_model_vao(data);
    model.diff_tex = diff;
    model.root = root;
    model.transforms = data->transforms;
    model.joint_count = data->joint_count;
    model.joints = data->joints;
    model.bind_shape_matrix = data->bind_shape_matrix;
    model.vertices_count = data->vertex_count;
    //calc_inv_bind_transform(&model.root,m4d(1.f));
    
    //not sure about this one -- let's figure out the parser first
    data->transforms = arena_alloc(&global_platform.permanent_storage, sizeof(mat4) * model.root.num_of_children);
    //initialize_joint_pos_array(&model.root,data->transforms);
    return model;
}
#endif

