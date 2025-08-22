# String Library for CCRP
# This file defines string manipulation functions that can be used in CCRP programs

# Function: length(str)
# Returns the length of a string
function length(str) {
    count = 0
    i = 0
    
    while str[i] != 0
        count = count + 1
        i = i + 1
    endwhile
    
    return count
}

# Function: substring(str, start, end)
# Returns a substring from start to end position
function substring(str, start, end) {
    if start < 0
        start = 0
    endif
    
    if end > length(str)
        end = length(str)
    endif
    
    if start >= end
        return ""
    endif
    
    result = ""
    i = start
    
    while i < end
        result = result + str[i]
        i = i + 1
    endwhile
    
    return result
}

# Function: concat(str1, str2)
# Concatenates two strings
function concat(str1, str2) {
    return str1 + str2
}

# Function: to_upper(str)
# Converts string to uppercase
function to_upper(str) {
    result = ""
    i = 0
    
    while str[i] != 0
        if str[i] >= 97 and str[i] <= 122
            result = result + (str[i] - 32)
        else
            result = result + str[i]
        endif
        i = i + 1
    endwhile
    
    return result
}

# Function: to_lower(str)
# Converts string to lowercase
function to_lower(str) {
    result = ""
    i = 0
    
    while str[i] != 0
        if str[i] >= 65 and str[i] <= 90
            result = result + (str[i] + 32)
        else
            result = result + str[i]
        endif
        i = i + 1
    endwhile
    
    return result
}

# Function: reverse(str)
# Reverses a string
function reverse(str) {
    len = length(str)
    result = ""
    i = len - 1
    
    while i >= 0
        result = result + str[i]
        i = i - 1
    endwhile
    
    return result
}

# Function: is_palindrome(str)
# Returns 1 if string is palindrome, 0 otherwise
function is_palindrome(str) {
    return str == reverse(str)
}

# Function: count_char(str, char)
# Counts occurrences of a character in string
function count_char(str, char) {
    count = 0
    i = 0
    
    while str[i] != 0
        if str[i] == char
            count = count + 1
        endif
        i = i + 1
    endwhile
    
    return count
}

# Function: find_char(str, char)
# Returns first position of character in string, -1 if not found
function find_char(str, char) {
    i = 0
    
    while str[i] != 0
        if str[i] == char
            return i
        endif
        i = i + 1
    endwhile
    
    return -1
}

# Function: replace_char(str, old_char, new_char)
# Replaces all occurrences of old_char with new_char
function replace_char(str, old_char, new_char) {
    result = ""
    i = 0
    
    while str[i] != 0
        if str[i] == old_char
            result = result + new_char
        else
            result = result + str[i]
        endif
        i = i + 1
    endwhile
    
    return result
}

# Function: trim(str)
# Removes leading and trailing spaces
function trim(str) {
    start = 0
    end = length(str) - 1
    
    # Find start of non-space characters
    while start <= end and str[start] == 32
        start = start + 1
    endwhile
    
    # Find end of non-space characters
    while end >= start and str[end] == 32
        end = end - 1
    endwhile
    
    if start > end
        return ""
    endif
    
    return substring(str, start, end + 1)
} 