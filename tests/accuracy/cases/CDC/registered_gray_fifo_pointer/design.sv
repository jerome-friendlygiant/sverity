module top(input logic src_clk,
  dst_clk,
  push,
  
           output logic [3:0] gray_dst);

  logic [3:0] binary = '0, gray_src = '0;
  logic [3:0] next_binary;
  (* async_reg = "true" *) logic [3:0] gray_meta;
  assign next_binary = binary + 1'b1;
  always_ff @(posedge src_clk) if (push) begin
    binary <= next_binary;
    gray_src <= (next_binary >> 1) ^ next_binary;
  end
  always_ff @(posedge dst_clk) begin
    gray_meta <= gray_src;
    gray_dst <= gray_meta;
  end

endmodule
