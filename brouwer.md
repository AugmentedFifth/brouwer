# brouwer (a scripting language)

## hello, world!

```haskell
print "hello, world!"
```

## factorial

```haskell
fn fac n
    case n
        0 => 1
        x => x * fac(n - 1)

-- or

fn fac2 n
    if n < 2
        1
    else
        n * fac2(n - 1)

n = read input
print (fac n)
```

More advanced version, exception handling and better typing:

```haskell
fn fac (n: Nat) -> Nat
    case n
        0 => 1
        x => x * fac(x - 1)

try
    n = read input
    print (fac n)
catch err
    print err
```

## calculator

```haskell
import Char (isDigit)


task calculator
    put "==> "
    var inp = input

    tokens = []
    var num = ""
    while length input > 0
        if isDigit input[0]
            num :> input[0]
        else
            if length num > 0
                tokens :> num
            tokens :> ("" :> input[0])

        input = input[1;]
    if length num > 0
        tokens :>


calculator
```

## water tower problem

where
    xs :> x
        xs.push(x)
