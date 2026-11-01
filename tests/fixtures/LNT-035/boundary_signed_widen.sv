// sverity-args: --top top
// expect: LNT-035 INFO 5
// expect-count: LNT-035 INFO 1
module top(input logic [5:0] n, input logic signed [7:0] s, input logic [7:0] u, output logic [63:0] w, output logic [7:0] same, output logic [63:0] c, output logic [63:0] v);
  assign w = s << n;                // signed 8-bit operand widened to 64 bits before a variable shift
  assign same = s << n;             // same width: no widening
  assign c = s << 2;                // constant amount: not analysed
  assign v = u << n;                // unsigned: no finding
endmodule
