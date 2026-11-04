module top(input logic a, b, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] src;
  always_ff @(posedge a) src <= d;
  always_ff @(posedge b) q <= src;
endmodule
