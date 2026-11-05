// A zero-power Phong lobe is constant. Handle it before pow(): 0^0 is
// undefined in shader arithmetic and can contaminate even a zero-colour light.
float JE_PhongLobe(float cosine, float power)
{
    if (power <= 0.0) return 1.0;
    const float base = saturate(cosine);
    return base > 0.0 ? pow(base, power) : 0.0;
}
