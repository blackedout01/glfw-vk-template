#include <stdint.h>

#define StaticAssert(Condition) struct sas_ ## __LINE__ { int A[-((Condition) == 0)]; } 

#define ArrayCount(X) ((sizeof(X)/sizeof(*X)))
#define MemberArrayCount(TypeName, MemberName) ArrayCount(((TypeName *)0)->MemberName)
#define Max(X, Y) ((X < Y)? (Y) : (X))
#define Min(X, Y) ((X < Y)? (X) : (Y))
#define Clamp(X, A, B) (Max(Min(X, B), A))

#define Align16(X, Type) ((((Type)(X)) + ((Type)15)) & (~((Type)15)))
#define AlignAny(X, Type, Alignment) ((((Type)(X)) + ((Type)(Alignment)) - 1) - ((((Type)(X)) + ((Type)(Alignment)) - 1) % ((Type)(Alignment))))

#define CODE_YELLOW "\033[0;33m"
#define CODE_RED "\033[0;31m"
#define CODE_RESET "\033[0m"
#define printfc(Color, Format, ...) printf("%s" Format "%s", Color, ##__VA_ARGS__, CODE_RESET)

#define CheckGoto(Result, Label) if(Result) goto Label
#define AssertMessage(Condition, Format, ...) if(!(Condition)) { printfc(CODE_RED, Format, ##__VA_ARGS__); }
#define AssertMessageGoto(Condition, Label, Format, ...) if(!(Condition)) { printfc(CODE_RED, Format, ##__VA_ARGS__); goto Label; }

#ifndef SIZE_T_MAX
#define SIZE_T_MAX ((size_t)-1)
#endif

#ifdef _WIN32
#include <windows.h>
#define SleepMilliseconds(Value) Sleep(Value)
#else
#include <unistd.h>
#define SleepMilliseconds(Value) usleep(1000*(Value))
#endif

typedef struct {
    float E[2];
} v2;

typedef struct {
    float E[3];
} v3;

typedef struct {
    float E[4];
} v4;

typedef struct {
    float E[4];
} m2;

typedef struct {
    float E[9];
} m3;

typedef struct {
    float E[16];
} m4;

static m4 IdentityM4() {
    m4 Result = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    return Result;
}

static m4 TransposeM4(m4 M) {
    m4 Result = {
        M.E[0], M.E[4], M.E[8], M.E[12],
        M.E[1], M.E[5], M.E[9], M.E[13],
        M.E[2], M.E[6], M.E[10], M.E[14],
        M.E[3], M.E[7], M.E[11], M.E[15],
    };
    return Result;
}

static m4 TranslationM4(float X, float Y, float Z) {
    m4 Result = {
        1.0f, 0.0f, 0.0f, X,
        0.0f, 1.0f, 0.0f, Y,
        0.0f, 0.0f, 1.0f, Z,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    return Result;
}

static m4 ScaleM4(float Scale, m4 M) {
    m4 Result = M;
    for(int I = 0; I < ArrayCount(Result.E); ++I) {
        Result.E[I] *= Scale;
    }
    return Result;
}

static m4 MultiplyM4M4(m4 A, m4 B) {
    m4 Result;
    for(int Row = 0; Row < 4; ++Row) {
        for(int Col = 0; Col < 4; ++Col) {
            float Val = 0.0;
            for(int I = 0; I < 4; ++I) {
                Val += A.E[4*Row + I]*B.E[4*I + Col];
            }
            Result.E[4*Row + Col] = Val;
        }
    }
    return Result;
}

static m4 AddM4M4(m4 A, m4 B) {
    m4 Result = A;
    for(int I = 0; I < ArrayCount(Result.E); ++I) {
        Result.E[I] += B.E[I];
    }
    return Result;
}

static m4 RotationM4(v3 Axis, float Rad) {
    m4 K = {
        0.0, -Axis.E[2], Axis.E[1], 0.0,
        Axis.E[2], 0.0, -Axis.E[0], 0.0,
        -Axis.E[1], Axis.E[0], 0.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    m4 KSq = MultiplyM4M4(K, K);
    

    m4 Result = AddM4M4(IdentityM4(), AddM4M4(ScaleM4(sinf(Rad), K), ScaleM4((1.0f - cosf(Rad)), KSq)));
    Result.E[15] = 1.0f;
    return Result;
}

static m4 ProjectionPersp(float FoVY, float WoH, float N, float F) {
    // NOTE(blackedout): Vulkan/Direct3D 0, OpenGL -1
    float NDCzNegN = 0.0f, NDCzNegF = 1.0f;
    // NOTE(blackedout): Vulkan -1, OpenGL(/Direct3D ?) 1
    float SignY = -1.0f;

    float TanY = tanf(0.5*FoVY);
    
    float SX = 1.0/(WoH*TanY);
    float SY = 1.0/TanY;
    float SZ = (NDCzNegF*F - NDCzNegN*N)/(N - F);
    float TZ = (NDCzNegF - NDCzNegN)*N*F/(N - F);
    
    m4 Result = {
         SX,      0.0,  0.0, 0.0,
        0.0, SignY*SY,  0.0, 0.0,
        0.0,      0.0,   SZ,  TZ,
        0.0,      0.0, -1.0, 0.0
    };
    return Result;
}

typedef struct {
    uint32_t Cap;
    uint32_t Count;
    uint32_t Next;
} buffer_indices;

static uint32_t IndicesCircularHead(buffer_indices *Indices) {
    // NOTE(blackedout): Returns the index of the last inserted element (head element)
    buffer_indices I = *Indices;
    return (I.Next + I.Cap - 1)%I.Cap;
}

static uint32_t IndicesCircularGet(buffer_indices *Indices, uint32_t Index) {
    // NOTE(blackedout): Returns the index of the ith inserted element (base + i circular)
    buffer_indices I = *Indices;
    return (I.Next + I.Cap - I.Count + Index)%I.Cap;
}

static uint32_t IndicesCircularPush(buffer_indices *Indices) {
    // NOTE(blackedout): Returns the index of the element that is to be pushed
    buffer_indices I = *Indices;
    AssertMessage(I.Count < I.Cap, "Can't push item because count reached the capacity of %d.\n", I.Cap);
    uint32_t PushIndex = I.Next;
    I.Next = (I.Next + 1)%I.Cap;
    ++I.Count;
    *Indices = I;
    return PushIndex;
}

static uint32_t IndicesCircularTake(buffer_indices *Indices) {
    // NOTE(blackedout): Return the index of the first inserted element (base element)
    buffer_indices I = *Indices;
    AssertMessage(I.Count > 0, "Can't take item because there are no elements.\n");
    uint32_t TakeIndex = IndicesCircularGet(&I, 0);
    --I.Count;
    *Indices = I;
    return TakeIndex;
}

typedef struct {
    void *Pointer;
    uint64_t ByteCount;
} malloc_multiple_subbuf;

static int MallocMultiple(int Count, malloc_multiple_subbuf *Subbufs, void **Result) {
    uint64_t ByteCountSum = 0;
    for(int I = 0; I < Count; ++I) {
        uint64_t ByteOffset = ByteCountSum;
        ByteCountSum = Align16(ByteCountSum + Subbufs[I].ByteCount, uint64_t);
        Subbufs[I].ByteCount = ByteOffset;
    }

    uint8_t *LocalResult = (uint8_t *)malloc(ByteCountSum);
    if(LocalResult == 0) {
        printf("Out of memory\n");
        return 1;
    }

    for(int I = 0; I < Count; ++I) {
        void **Write = (void **)Subbufs[I].Pointer; // NOTE(blackedout): Otherwise there are compiler issues when initializing the subbufs
        *Write = (void *)(LocalResult + Subbufs[I].ByteCount);
    }
    *Result = (void * *)LocalResult;
    return 0;
}

static int LoadFileContentsCStd(const char *Filepath, uint8_t **FileBytes, uint64_t *FileByteCount) {
    int Result = 1;
    long int LocalFileByteCount;
    uint8_t *LocalFileBytes;

    FILE *File = fopen(Filepath, "rb");
    AssertMessageGoto(File, label_Exit, "File '%s' could not be opened (code %d).\n", Filepath, errno);

    if(fseek(File, 0, SEEK_END) != 0) {
        printf("fseek with SEEK_END failed for file '%s'.\n", Filepath);
        goto label_FileOpen;
    }

    LocalFileByteCount = ftell(File);
    AssertMessageGoto(LocalFileByteCount != -1L, label_FileOpen, "ftell failed for file '%s' (code %d).\n", Filepath, errno);
    rewind(File);

    LocalFileBytes = (uint8_t *)malloc(LocalFileByteCount + 1);
    AssertMessageGoto(LocalFileBytes != 0, label_FileOpen, "File '%s' could not be read: out of memory.\n", Filepath);
    LocalFileBytes[LocalFileByteCount] = 0;

    if(fread(LocalFileBytes, 1, LocalFileByteCount, File) != (size_t)LocalFileByteCount) {
        printf("File '%s' failed to read.\n", Filepath);
        goto label_FileOpen;
    }

    *FileByteCount = (uint64_t)LocalFileByteCount;
    *FileBytes = LocalFileBytes;
    Result = 0;
    goto label_FileOpen;

label_Memory:
    free(LocalFileBytes);
label_FileOpen:
    fclose(File);
label_Exit:
    return Result;
}