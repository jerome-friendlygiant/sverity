// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// CDC-003 is evidence of the primary CDC-028 finding
// expect: CDC-028 ERROR
// expect-none: CDC-003 INFO
// revert: first <= en ? launched : first; => first <= launched;
// revert-expect-none: CDC-028 ERROR
// revert-expect-none: CDC-028 ERROR
module top(input logic clka, clkb, en, data, output logic result);
  logic launched, first, second;
  always_ff @(posedge clka) launched <= data;
  always_ff @(posedge clkb) begin
    first <= en ? launched : first;
    second <= first;
  end
  assign result = second;
endmodule
