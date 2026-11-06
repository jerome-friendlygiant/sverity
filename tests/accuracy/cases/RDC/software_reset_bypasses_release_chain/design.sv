module top(input logic src_clk,
  dst_clk,
  por_n,
  sw_reset,
  
           output logic ready);

  logic por_meta, por_sync, reset_src = 1;
  wire local_n = por_sync & ~reset_src;
  always_ff @(posedge src_clk) reset_src <= sw_reset;
  always_ff @(posedge dst_clk or negedge por_n) begin
    if (!por_n) begin por_meta <= 0; por_sync <= 0; end
    else begin por_meta <= 1; por_sync <= por_meta; end
  end
  always_ff @(posedge dst_clk or negedge local_n) begin
    if (!local_n) ready <= 0;
    else ready <= 1;
  end

endmodule
