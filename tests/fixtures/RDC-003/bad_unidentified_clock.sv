// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// the clock event is an expression, so the clock is unidentified; the register's asynchronous reset must not silently drop out of the reset checks
// expect: RDC-003 WARNING
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clk1, en, rst_n, d, output logic q);
  always_ff @(posedge (clk1 & en) or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
