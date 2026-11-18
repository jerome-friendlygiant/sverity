// sverity-args: --top top
// `x` feeds itself bit for bit through the xor, whatever the computed index in the other operand reads: a certain loop.
// expect: LNT-012 ERROR 9
// expect-none: LNT-012 WARNING
module top(input logic [1:0] idx, input logic [7:0] d, output logic [7:0] y);
  logic [7:0] mem [4];
  logic [7:0] x;
  assign mem = '{d, ~d, d ^ 8'h5a, d + 8'd1};
  assign x = mem[idx] ^ x;
  assign y = x;
endmodule
