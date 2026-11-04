module top(input logic a, b, d, output logic q);
  logic src;
  always_ff @(posedge a) src <= d;
  // sverity-waive CDC-003 owner="example" reason="protocol reviewed"
  always_ff @(posedge b) q <= src;
endmodule
