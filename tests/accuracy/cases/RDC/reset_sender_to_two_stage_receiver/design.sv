module top(input logic clk,
  source_reset_n,
  set_alarm,
  output logic alarm_out);

  logic alarm_source;
  (* async_reg = "true" *) logic alarm_meta;
  always_ff @(posedge clk or negedge source_reset_n) begin
    if (!source_reset_n) alarm_source <= 0;
    else if (set_alarm) alarm_source <= 1;
  end
  always_ff @(posedge clk) begin
    alarm_meta <= alarm_source;
    alarm_out <= alarm_meta;
  end

endmodule
