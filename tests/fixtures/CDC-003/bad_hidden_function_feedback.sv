// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-003 ERROR
// expect-none: CDC-003 INFO
// revert: return launched ^ first; => return launched;
// revert-expect-none: CDC-003 ERROR
module top(input logic clka, clkb, value, output logic result);
  logic launched, first, second;
  always_ff @(posedge clka) launched <= value;
  function automatic logic update(); return launched ^ first; endfunction
  always_ff @(posedge clkb) begin first <= update(); second <= first; end
  assign result = second;
endmodule
