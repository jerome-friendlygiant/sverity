// sverity-args: --top top
// expect: LNT-082 INFO
module top(input logic [1:0] a, output logic [3:0] q); logic [3:0] mem [4]; assign q=mem[a]; endmodule
