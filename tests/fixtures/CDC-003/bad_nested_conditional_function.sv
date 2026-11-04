// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-003 ERROR
// revert: return remote; => return 1'b0;
// revert-expect-none: CDC-003 ERROR
module top(input logic clka, clkb, value, choose, output logic result);
  logic remote;
  always_ff @(posedge clka) remote <= value;
  function automatic logic inner_read(input logic selector);
    if (selector) return remote;
    else return 0;
  endfunction
  function automatic logic outer_read(input logic selector); return inner_read(selector); endfunction
  always_ff @(posedge clkb) result <= outer_read(choose);
endmodule
