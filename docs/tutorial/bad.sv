module top(input logic a, b, d, output logic q);
  logic src;
  always_ff @(posedge a) src <= d;
  always_ff @(posedge b) q <= src;
endmodule
