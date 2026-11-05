// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// CDC-003 is evidence of the primary CDC-028 finding
// expect: CDC-028 ERROR
module top(input logic clka, clkb, d, enable, output logic q);
  logic src, guard, a, b;
  always_ff @(posedge clka) begin src <= d; guard <= enable; end
  always_ff @(posedge clkb) begin
    a <= src; b <= a;
    if (guard) begin a <= 0; b <= 0; end
  end
  assign q = b;
endmodule
