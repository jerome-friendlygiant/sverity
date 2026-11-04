// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CST-010 WARNING
// expect-coverage: graph-precision INCOMPLETE
// expect: CDC-003 ERROR
module top(input logic clka, clkb, value, input logic [3:0] depth, output logic result);
  logic remote;
  always_ff @(posedge clka) remote <= value;
  function automatic logic descend(input logic [3:0] n);
    if (n == 0) return remote;
    return descend(n - 1'b1);
  endfunction
  always_ff @(posedge clkb) result <= descend(depth);
endmodule
