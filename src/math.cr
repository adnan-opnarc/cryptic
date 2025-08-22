# Math Library for CCRP
# This file defines mathematical functions that can be used in CCRP programs

# Function: sqrt(number)
# Returns the square root of a number
function sqrt(x) {
    if x < 0
        return 0
    endif
    
    result = 1
    temp = 0
    
    while result != temp
        temp = result
        result = (result + x / result) / 2
    endwhile
    
    return result
}

# Function: abs(number)
# Returns the absolute value of a number
function abs(x) {
    if x < 0
        return -x
    else
        return x
    endif
}

# Function: pow(base, exponent)
# Returns base raised to the power of exponent
function pow(base, exp) {
    if exp == 0
        return 1
    endif
    
    if exp < 0
        return 1 / pow(base, -exp)
    endif
    
    result = 1
    i = 0
    
    while i < exp
        result = result * base
        i = i + 1
    endwhile
    
    return result
}

# Function: max(a, b)
# Returns the maximum of two numbers
function max(a, b) {
    if a > b
        return a
    else
        return b
    endif
}

# Function: min(a, b)
# Returns the minimum of two numbers
function min(a, b) {
    if a < b
        return a
    else
        return b
    endif
}

# Function: mod(a, b)
# Returns the remainder of a divided by b
function mod(a, b) {
    if b == 0
        return 0
    endif
    
    return a - (a / b) * b
}

# Function: factorial(n)
# Returns the factorial of n
function factorial(n) {
    if n <= 1
        return 1
    endif
    
    result = 1
    i = 2
    
    while i <= n
        result = result * i
        i = i + 1
    endwhile
    
    return result
}

# Function: gcd(a, b)
# Returns the greatest common divisor of two numbers
function gcd(a, b) {
    a = abs(a)
    b = abs(b)
    
    while b != 0
        temp = b
        b = mod(a, b)
        a = temp
    endwhile
    
    return a
}

# Function: lcm(a, b)
# Returns the least common multiple of two numbers
function lcm(a, b) {
    return abs(a * b) / gcd(a, b)
}

# Function: is_prime(n)
# Returns 1 if n is prime, 0 otherwise
function is_prime(n) {
    if n < 2
        return 0
    endif
    
    if n == 2
        return 1
    endif
    
    if mod(n, 2) == 0
        return 0
    endif
    
    i = 3
    while i * i <= n
        if mod(n, i) == 0
            return 0
        endif
        i = i + 2
    endwhile
    
    return 1
}

# Function: fibonacci(n)
# Returns the nth Fibonacci number
function fibonacci(n) {
    if n <= 0
        return 0
    endif
    
    if n == 1
        return 1
    endif
    
    a = 0
    b = 1
    i = 2
    
    while i <= n
        temp = a + b
        a = b
        b = temp
        i = i + 1
    endwhile
    
    return b
} 