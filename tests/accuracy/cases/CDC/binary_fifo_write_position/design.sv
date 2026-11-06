module top(input logic src_clk,
  dst_clk,
  push,
  
           output logic [3:0] write_position_dst);

  logic [3:0] write_position = '0;
  logic [3:0] pointer_meta;
  always_ff @(posedge src_clk) if (push) write_position <= write_position + 1'b1;
  always_ff @(posedge dst_clk) begin
    pointer_meta <= write_position;
    write_position_dst <= pointer_meta;
  end

endmodule
