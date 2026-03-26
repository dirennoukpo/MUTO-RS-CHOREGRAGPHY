#include <iostream>
#include <vector>
#include <cmath>

class IForme3D {
    public:
        virtual ~IForme3D() {}
        virtual double Perimeter() const = 0;
        virtual double AF() const = 0;
        virtual double Volume() const = 0;
};

class Cube : public IForme3D {
    public:
        Cube(double cote);
        double Perimeter() const override;
        double AF() const override;
        double Volume() const override;
    private:
        double _cote;
};

class Sphere : public IForme3D {
    public:
        Sphere(double rayon);
        double Perimeter() const override;
        double AF() const override;
        double Volume() const override;
    private:
        double _rayon;
};

Cube::Cube(double cote) : _cote(cote)
{
}

double Cube::Perimeter() const
{
    return 12 * _cote;
}

double Cube::AF() const
{
    return 6 * std::pow(_cote, 2);
}

double Cube::Volume() const
{
    return std::pow(_cote, 3);
}

Sphere::Sphere(double rayon) : _rayon(rayon)
{
}

double Sphere::Perimeter() const
{
    return 0;
}

double Sphere::AF() const
{
    return 4 * M_PI * std::pow(_rayon, 2);
}

double Sphere::Volume() const
{
    return (4.0 / 3.0) * M_PI * std::pow(_rayon, 3);
}

void afficherRapport(const IForme3D& forme)
{
    std::cout << "P: " << forme.Perimeter() << " | ";

    std::cout << "A: " << forme.AF() << " | ";

    std::cout << "V: " << forme.Volume() << std::endl;
}

int main()
{
    Cube c(3.0);
    Sphere s(5.0);

    IForme3D* formes[] = { &c, &s };
    for (IForme3D* f : formes) {
        afficherRapport(*f);
    }
    return 0;
}
