// sverity-args: --top top
// An instance output drives word 2, and a clocked process also writes word 2.
// expect: LNT-049 WARNING 8
module leaf(input logic [3:0] d, output logic [3:0] q);
  assign q = ~d;
endmodule
module top(input logic clk, we, input logic [3:0] d, output logic [3:0] y);
  logic [3:0] m [4];
  leaf u_a (.d(d), .q(m[2]));
  always_ff @(posedge clk) if (we) m[2] <= d;
  assign y = m[2];
endmodule
