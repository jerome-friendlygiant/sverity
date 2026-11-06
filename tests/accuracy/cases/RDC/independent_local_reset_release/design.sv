module release_reset(input logic clk,
  por_n,
  output logic local_n);

  (* async_reg = "true" *) logic first_n;
  always_ff @(posedge clk or negedge por_n) begin
    if (!por_n) begin first_n <= 0; local_n <= 0; end
    else begin first_n <= 1; local_n <= first_n; end
  end

endmodule
module top(input logic src_clk,
  dst_clk,
  por_n,
  output logic a,
  b);

  wire ra_n, rb_n;
  release_reset a_reset(src_clk, por_n, ra_n);
  release_reset b_reset(dst_clk, por_n, rb_n);
  always_ff @(posedge src_clk or negedge ra_n)
    if (!ra_n) a <= 0; else a <= 1;
  always_ff @(posedge dst_clk or negedge rb_n)
    if (!rb_n) b <= 0; else b <= 1;

endmodule
