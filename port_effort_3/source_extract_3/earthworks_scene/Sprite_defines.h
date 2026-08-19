
#include "Falcor.h"

struct _spriteVertex_packed
{
    glm::vec3	pos;		// 12
	unsigned int	idx;		//  4  [........][.......R][uuuuvvvv][dududvdv]
    glm::vec3	up;			// 12
	unsigned int	w;			//  4  [wwwwwwww][wwwwwwww][ssssssss][aaaaaaaa]
};


struct _spriteVertex
{
	glm::vec3	pos;
	glm::vec3	up;
	float	width;
	UCHAR	u;
	UCHAR	v;
	UCHAR	du;
	UCHAR	dv;
	float	AO;
	float	shadow;
	bool	bFreeRotate;
	bool	bFadeout;
};

