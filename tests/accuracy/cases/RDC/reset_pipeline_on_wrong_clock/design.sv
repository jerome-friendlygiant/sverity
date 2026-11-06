module top(input logic src_clk,
  dst_clk,
  por_n,
  enable,
  output logic [3:0] count);

  logic reset_meta, reset_src;
  always_ff @(posedge src_clk or negedge por_n) begin
    if (!por_n) begin reset_meta <= 0; reset_src <= 0; end
    else begin reset_meta <= 1; reset_src <= reset_meta; end
  end
  always_ff @(posedge dst_clk or negedge reset_src) begin
    if (!reset_src) count <= '0;
    else if (enable) count <= count + 1'b1;
  end

endmodule
