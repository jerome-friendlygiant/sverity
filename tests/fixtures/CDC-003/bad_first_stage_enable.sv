// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// CDC-003 is evidence of the primary CDC-028 finding
// expect: CDC-028 ERROR
// expect-none: CDC-003 INFO
// revert: if (en) first <= source; => first <= source;
// revert-expect-none: CDC-028 ERROR
// revert-expect-none: CDC-028 ERROR
module top(input logic clka, clkb, en, d, output logic q);
  logic source, first, second;
  always_ff @(posedge clka) source <= d;
  always_ff @(posedge clkb) begin
    if (en) first <= source;
    second <= first;
  end
  assign q = second;
endmodule
