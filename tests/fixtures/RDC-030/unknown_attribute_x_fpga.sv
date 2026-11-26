// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --profile fpga
// The second stage's marker is 1'bx: it cannot establish or refute the marking, so the finding is Unresolved and not suppressed.
// expect-unresolved: RDC-030
module top(input logic clka, clkb, rst_n, d, output logic q);
  (* ASYNC_REG = "TRUE" *) logic s1;
  (* ASYNC_REG = 1'bx *) logic s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) s1 <= 1'b0; else s1 <= 1'b1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clka or negedge s2) if (!s2) q <= 1'b0; else q <= d;
endmodule
