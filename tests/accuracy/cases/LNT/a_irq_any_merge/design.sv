`ifdef STRICT_NETS
`default_nettype none
`endif
// Merges two masked interrupt sources into one registered request.
// A set bit in irq_mask blocks the matching source. irq_cnt counts the
// cycles in which the merged request was raised.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [1:0] irq_src,
  input  logic [1:0] irq_mask,
  output logic       irq_o,
  output logic [3:0] irq_cnt
);
  logic       irq_q;
  logic [3:0] cnt_q;

  assign irq_any = |(irq_src & ~irq_mask);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      irq_q <= 1'b0;
      cnt_q <= 4'd0;
    end else begin
      irq_q <= irq_any;
      if (irq_q && cnt_q != 4'hF) cnt_q <= cnt_q + 4'd1;
    end
  end

  assign irq_o   = irq_q;
  assign irq_cnt = cnt_q;
endmodule
