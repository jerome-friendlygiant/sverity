// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// set_n is tested only inside a nested case, not by the top-level if: its forced value is not analysed, but it is still an
// asynchronous control, so the register has two independent reset sources (RDC-011). Dropping it hid the second source.
// expect: RDC-011 WARNING
// expect-unresolved: RDC-026
module top(input logic clka, rst_n, set_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n or negedge set_n)
    if (!rst_n) q <= 1'b0; else case (set_n) 1'b0: q <= 1'b1; default: q <= d; endcase
endmodule
