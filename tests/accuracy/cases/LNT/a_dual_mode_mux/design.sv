// Output stage that is either registered or passed through, chosen at build time.
// With REGISTERED set the data has one cycle of latency; otherwise it is
// a wire. any_set reports whether the delivered byte has any bit set.
module top #(
  parameter bit REGISTERED = 1'b1
) (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] d,
  output logic [7:0] q,
  output logic       any_set
);
  logic [7:0] out_w;

  if (REGISTERED) begin : g_reg
    logic [7:0] q_q;
    always_ff @(posedge clk or negedge rst_n) begin
      if (!rst_n) q_q <= 8'h00;
      else        q_q <= d;
    end
    assign out_w = q_q;
  end else begin : g_pass
    assign out_w = d;
  end

  assign q       = out_w;
  assign any_set = |out_w;
endmodule
