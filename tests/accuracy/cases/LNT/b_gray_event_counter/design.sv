// Event counter with a Gray-coded copy for crossing into another clock domain.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       en,
  output logic [3:0] gray_q,
  output logic       wrap
);
  logic [3:0] bin_q;
  logic [3:0] bin_next;
  logic [3:0] gray_next;

  assign bin_next  = en ? bin_q + 4'd1 : bin_q;
  assign gray_next = bin_next ^ (bin_next >> 1);
  assign wrap      = en & (bin_q == 4'hF);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      bin_q  <= 4'd0;
      gray_q <= 4'd0;
    end else begin
      bin_q  <= bin_next;
      gray_q <= gray_next;
    end
  end
endmodule
