// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-003 ERROR
// revert: (enable & launched) | (~enable & first) => launched
// revert-expect-none: CDC-003 ERROR
module top(input logic clka, clkb, enable, value, output logic result);
  logic launched, first, second;
  always_ff @(posedge clka) launched <= value;
  always_ff @(posedge clkb) begin
    first <= (enable & launched) | (~enable & first);
    second <= first;
  end
  assign result = second;
endmodule
