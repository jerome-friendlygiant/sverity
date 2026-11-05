// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-028 ERROR
// expect-none: CDC-003 INFO
// revert: first = launch; second = first; => first <= launch; second <= first;
// revert: first = 0; second = 0; => first <= 0; second <= 0;
// revert-expect-none: CDC-028 ERROR
module top(input logic clka, clkb, rst, d, output logic q);
  logic launch, first, second;
  always_ff @(posedge clka) launch <= d;
  always_ff @(posedge clkb) begin
    first = launch; second = first;
    if (rst) begin first = 0; second = 0; end
  end
  assign q = second;
endmodule
