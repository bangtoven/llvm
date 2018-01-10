#include<stdio.h>
int main()
{

    int i =0;
    int a[2000]={0};
    for (i =0; i < 1995;i++)
    {

            a[i] = a[i+4]+5;
    }

for (int j =0; j < 5;j++)
    {

for (i =0; i < 1995;i++)
    {

            a[i] = a[i+4]+5;
    }
    }

for (i =0; i < 1995;i++)
    {

            a[i] = a[i+4]+5;
    }

    return 1;
}