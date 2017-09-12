| name        | opcode | args       | stack transform             | description                                                                                              |
| ----------- | ------ | ---------- | --------------------------- | -------------------------------------------------------------------------------------------------------- |
| `nop`       | `0x00` |            |                             | does nothing.                                                                                            |
| `iconst_n1` | `0x01` |            | ⇒ `-1i64`                   | push `-1` as a signed integer onto the stack.                                                            |
| `iconst_0`  | `0x02` |            | ⇒ `0i64`                    | push `0` as a signed integer onto the stack.                                                             |
| `iconst_1`  | `0x03` |            | ⇒ `1i64`                    | push `1` as a signed integer onto the stack.                                                             |
| `iconst_2`  | `0x04` |            | ⇒ `2i64`                    | push `2` as a signed integer onto the stack.                                                             |
| `iconst_3`  | `0x05` |            | ⇒ `3i64`                    | push `3` as a signed integer onto the stack.                                                             |
| `fconst_n1` | `0x06` |            | ⇒ `-1f64`                   | push `-1` as a floating point number onto the stack.                                                     |
| `fconst_0`  | `0x07` |            | ⇒ `0f64`                    | push `0` as a floating point number onto the stack.                                                      |
| `fconst_1`  | `0x08` |            | ⇒ `1f64`                    | push `1` as a floating point number onto the stack.                                                      |
| `fconst_2`  | `0x09` |            | ⇒ `2f64`                    | push `2` as a floating point number onto the stack.                                                      |
| `fconst_3`  | `0x0A` |            | ⇒ `3f64`                    | push `3` as a floating point number onto the stack.                                                      |
| `load_0`    | `0x0B` |            | ⇒ `val`                     | push the value from local variable 0 onto the stack.                                                     |
| `load_1`    | `0x0C` |            | ⇒ `val`                     | push the value from local variable 1 onto the stack.                                                     |
| `load_2`    | `0x0D` |            | ⇒ `val`                     | push the value from local variable 2 onto the stack.                                                     |
| `load_3`    | `0x0E` |            | ⇒ `val`                     | push the value from local variable 3 onto the stack.                                                     |
| `load`      | `0x0F` | `local_ix` | ⇒ `val`                     | push the value from the local variable at `local_ix` onto the stack.                                     |
| `store_0`   | `0x10` |            | `val` ⇒                     | pop the top of the stack and store it in local variable 0.                                               |
| `store_1`   | `0x11` |            | `val` ⇒                     | pop the top of the stack and store it in local variable 1.                                               |
| `store_2`   | `0x12` |            | `val` ⇒                     | pop the top of the stack and store it in local variable 2.                                               |
| `store_3`   | `0x13` |            | `val` ⇒                     | pop the top of the stack and store it in local variable 3.                                               |
| `store`     | `0x14` | `local_ix` | `val` ⇒                     | pop the top of the stack and store it in the local variable at `local_ix`.                               |
| `pop`       | `0x15` |            | `val` ⇒                     | pop the top of the stack and discard it.                                                                 |
| `dup`       | `0x16` |            | `val` ⇒ `val, val`          | duplicates the top of the stack.                                                                         |
| `swap`      | `0x17` |            | `val2, val1` ⇒ `val1, val2` | swaps the top two values on the stack.                                                                   |
| `iadd`      | `0x18` |            | `int1, int2` ⇒ `int3`       | adds two integers.                                                                                       |
| `fadd`      | `0x19` |            | `float1, float2` ⇒ `float3` | adds two floating point numbers.                                                                         |
| `isub`      | `0x1A` |            | `int1, int2` ⇒ `int3`       | subtracts two integers.                                                                                  |
| `fsub`      | `0x1B` |            | `float1, float2` ⇒ `float3` | subtracts two floating point numbers.                                                                    |
| `imul`      | `0x1C` |            | `int1, int2` ⇒ `int3`       | multiplies two integers.                                                                                 |
| `fmul`      | `0x1D` |            | `float1, float2` ⇒ `float3` | multiplies two floating point numbers.                                                                   |
| `idiv`      | `0x1E` |            | `int1, int2` ⇒ `int3`       | divides two integers.                                                                                    |
| `fdiv`      | `0x1F` |            | `float1, float2` ⇒ `float3` | divides two floating point numbers.                                                                      |
| `imod`      | `0x20` |            | `int1, int2` ⇒ `int3`       | takes one integer modulo another.                                                                        |
| `fmod`      | `0x21` |            | `float1, float2` ⇒ `float3` | takes one floating point number modulo another.                                                          |
| `ineg`      | `0x22` |            | `int1` ⇒ `int2`             | negates an integer.                                                                                      |
| `fneg`      | `0x23` |            | `float1` ⇒ `float2`         | negates a floating point number.                                                                         |
| `i2f`       | `0x24` |            | `int` ⇒ `float`             | converts an integer to a floating point number.                                                          |
| `f2i`       | `0x25` |            | `float` ⇒ `int`             | converts a floating point number to an integer.                                                          |
| `icmp`      | `0x26` |            | `int1, int2` ⇒ `int3`       | compares two integers, resulting in `0` when `int1 == int2`, `1` when `int1 > int2`, and `-1` otherwise. |
| `fcmpl`
