// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_two_async_controls.sv: the set control is removed from the sensitivity list and the branch, so the register has one asynchronous control.
// expect-none: RDC-026
module top(input logic clka, rst_n, set_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
