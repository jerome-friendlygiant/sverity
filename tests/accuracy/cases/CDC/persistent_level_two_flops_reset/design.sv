module top(input logic src_clk,
  dst_clk,
  src_clear,
  dst_clear,
  set_alarm,
  
           output logic alarm_dst);

  logic alarm_src;
  (* async_reg = "true" *) logic alarm_meta;
  always_ff @(posedge src_clk) begin
    if (src_clear) alarm_src <= 1'b0;
    else if (set_alarm) alarm_src <= 1'b1;
  end
  always_ff @(posedge dst_clk) begin
    if (dst_clear) begin alarm_meta <= 1'b0; alarm_dst <= 1'b0; end
    else begin alarm_meta <= alarm_src; alarm_dst <= alarm_meta; end
  end

endmodule
