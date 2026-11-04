// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-003 ERROR
module top(input logic clka, clkb, en, d, other, output logic q);
  logic src, a, b;
  always_ff @(posedge clka) src <= d;
  always_ff @(posedge clkb) begin
    a <= src; b <= a;
    if (en) b <= other;
  end
  assign q = b;
endmodule
