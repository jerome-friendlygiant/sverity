// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-003 ERROR
// revert: return launched; => return 1'b0;
// revert-expect-none: CDC-003 ERROR
module top(input logic clka, clkb, value, output logic result);
  logic launched;
  always_ff @(posedge clka) launched <= value;
  function automatic logic sample_state(); return launched; endfunction
  always_ff @(posedge clkb) result <= sample_state();
endmodule
