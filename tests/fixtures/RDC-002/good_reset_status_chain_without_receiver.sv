// sverity-args: --top top --sdc @DIR@/release_safe.sdc
// A two-stage reset chain exported only as a status output is a synchroniser by shape: the raw reset input needs no constraint.
// expect-none: RDC-002
module top(input logic clk, input logic por_ni, input logic d_i, input logic en_i, output logic ready_o);
  logic [1:0] rs_q;
  always_ff @(posedge clk or negedge por_ni) if (!por_ni) rs_q <= 2'b00; else rs_q <= {rs_q[0], 1'b1};
  assign ready_o = rs_q[1];
endmodule
