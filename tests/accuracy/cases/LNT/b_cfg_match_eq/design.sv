// Counts accepted tags that fall in the monitored address window.
// Interface:
//   valid       tag is valid this cycle
//   tag         4-bit tag from the request decoder
//   match_cnt   saturating count of window hits
//   seen        sticky flag, set on the first hit since reset
//
// The window is the set of tags whose bits 3, 2 and 0 are 1, 0 and 1.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       valid,
  input  logic [3:0] tag,
  output logic [7:0] match_cnt,
  output logic       seen
);
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      match_cnt <= 8'd0;
      seen      <= 1'b0;
    end else if (valid && (tag == 4'b10x1)) begin
      if (match_cnt != 8'hFF) match_cnt <= match_cnt + 8'd1;
      seen <= 1'b1;
    end
  end
endmodule
