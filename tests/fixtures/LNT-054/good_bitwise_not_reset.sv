// sverity-args: --top top
// The asynchronous reset is tested with a bitwise NOT of a one-bit signal; every register of the block is reset.
// expect-none: LNT-054
module top(input logic clk_i, rst_ni, d, output logic y);
  logic stopped_q, restoring_q;
  always_ff @(posedge clk_i or negedge rst_ni) begin : proc_ctrl
    if (~rst_ni) begin
      stopped_q <= 1'b0;
      restoring_q <= 1'b0;
    end else begin
      stopped_q <= d ^ stopped_q;
      restoring_q <= stopped_q ? ~d : restoring_q;
    end
  end
  assign y = (!stopped_q && d) ? restoring_q : 1'b0;
endmodule
