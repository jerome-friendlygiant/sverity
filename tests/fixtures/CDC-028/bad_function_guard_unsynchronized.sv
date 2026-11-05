// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// CDC-003 is evidence of the primary CDC-028 finding
// expect: CDC-028 ERROR
// revert: return source; => return 1'b0;
// revert-expect-none: CDC-028 ERROR
module top(input logic clka, clkb, value, data, output logic result);
  logic source;
  always_ff @(posedge clka) source <= value;
  function automatic logic gate_value(); return source; endfunction
  always_ff @(posedge clkb) if (gate_value()) result <= data;
endmodule
