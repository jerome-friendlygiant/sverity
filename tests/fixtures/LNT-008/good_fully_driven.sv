// sverity-args: --top top
// expect-none: LNT-008
module top(input logic [3:0] a, input logic [7:0] b, output logic [3:0] y, output logic [7:0] z);
  wire [3:0] w;
  wire [7:0] v;
  assign w[3:0] = a;
  assign v[3:0] = a;
  assign v[7:4] = a;
  assign y = w;
  assign z = v ^ b;
endmodule
