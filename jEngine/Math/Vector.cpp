#include <pch.h>
#include "Vector.h"
#include "Matrix.h"

const Vector Vector::OneVector = Vector(1.0f);
const Vector Vector::ZeroVector = Vector(ZeroType);
const Vector Vector::FowardVector = Vector(0.0f, 0.0f, 1.0f);
const Vector Vector::RightVector = Vector(1.0f, 0.0f, 0.0f);
const Vector Vector::UpVector = Vector(0.0f, 1.0f, 0.0f);

Vector Vector::GetEulerAngleFrom() const
{
	// Vector::UpVector 이 기반 회전 벡터 방향으로 가정함.
	return Vector(acosf(y), atan2f(x, z), 0.0f);
}

Vector Vector::GetDirectionFromEulerAngle() const
{
	return Matrix::MakeRotate(x, y, z).TransformDirection(Vector::UpVector);
}

// Vector
void Vector::operator=(struct Vector2 const& vector)
{
	x = vector.x;
	y = vector.y;
	z = 0.0f;
}

void Vector::operator=(struct Vector4 const& vector)
{
	x = vector.x;
	y = vector.y;
	z = vector.y;
}

//////////////////////////////////////////////////////////////////////////

// Vector4
const Vector4 Vector4::OneVector = Vector4(1.0f);
const Vector4 Vector4::ZeroVector = Vector4(ZeroType);
const Vector4 Vector4::FowardVector = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
const Vector4 Vector4::RightVector = Vector4(1.0f, 0.0f, 0.0f, 0.0f);
const Vector4 Vector4::UpVector = Vector4(0.0f, 1.0f, 0.0f, 0.0f);
const Vector4 Vector4::ColorRed = Vector4(1.0f, 0.0f, 0.0f, 1.0f);
const Vector4 Vector4::ColorGreen = Vector4(0.0f, 1.0f, 0.0f, 1.0f);
const Vector4 Vector4::ColorBlue = Vector4(0.0f, 0.0f, 1.0f, 1.0f);
const Vector4 Vector4::ColorWhite = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
const Vector4 Vector4::ColorBlack = Vector4(0.0f, 0.0f, 0.0f, 1.0f);

Vector4::Vector4(const Vector& InVector)
	: x(InVector.x), y(InVector.y), z(InVector.z), w(0.0f)
{
}

Vector4::Vector4(const Vector2& InA, const Vector2& InB)
	: x(InA.x), y(InA.y), z(InB.x), w(InB.y)
{
}

void Vector4::operator=(struct Vector2 const& vector)
{
	x = vector.x;
	y = vector.y;
	z = 0.0f;
	w = 0.0f;
}

void Vector4::operator=(struct Vector const& vector)
{
	x = vector.x;
	y = vector.y;
	z = vector.z;
	w = 0.0f;
}

//////////////////////////////////////////////////////////////////////////


// Vector2
const Vector2 Vector2::OneVector(1.0f);
const Vector2 Vector2::ZeroVector(0.0f);

Vector2::Vector2(const Vector& InVector)
	: x(InVector.x), y(InVector.y)
{
}

Vector2::Vector2(const Vector4& InVector)
	: x(InVector.x), y(InVector.y)
{
}

void Vector2::operator=(struct Vector const& vector)
{
	x = vector.x;
	y = vector.y;
}

void Vector2::operator=(struct Vector4 const& vector)
{
	x = vector.x;
	y = vector.y;
}
//////////////////////////////////////////////////////////////////////////
