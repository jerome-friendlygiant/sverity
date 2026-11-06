module top(input logic src_clk,
  dst_clk,
  load,
  
  input logic [3:0] config_in,
  output logic [3:0] config_dst);

  logic [3:0] config_src;
  (* async_reg = "true" *) logic [3:0] config_meta;
  always_ff @(posedge src_clk) if (load) config_src <= config_in;
  always_ff @(posedge dst_clk) begin
    config_meta <= config_src;
    config_dst <= config_meta;
  end

endmodule
