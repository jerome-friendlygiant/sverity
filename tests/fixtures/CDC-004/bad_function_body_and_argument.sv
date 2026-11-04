// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-004 ERROR
// revert: return stored ^ mask; => return mask;
// revert-expect-none: CDC-004 ERROR
module top(input logic clka, clkb, input logic [3:0] value, select_mask, output logic [3:0] result);
  logic [3:0] stored;
  always_ff @(posedge clka) stored <= value;
  function automatic logic [3:0] combine(input logic [3:0] mask);
    return stored ^ mask;
  endfunction
  always_ff @(posedge clkb) result <= combine(select_mask);
endmodule
