// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-013 WARNING
module top(input logic clka, clkb, d, output logic q);
  logic src, s1, s2;
  always_ff @(posedge clka) src <= d;
  always_ff @(posedge clkb) begin s1 <= src; s2 <= s1; end
  assign q = s2;
endmodule
