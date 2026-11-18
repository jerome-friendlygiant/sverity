// Prescaler producing one tick every 200 enabled clocks.
module tick_gen (
  input  logic clk,
  input  logic rst_n,
  input  logic enable,
  output logic tick
);
  logic [7:0] div;
  assign tick = enable & (div == 8'd199);
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)                div <= 8'd0;
    else if (!enable)          div <= 8'd0;
    else if (div == 8'd199)    div <= 8'd0;
    else                       div <= div + 8'd1;
  end
endmodule

// Seconds counter rolling over at 60.
module sec_counter (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       tick,
  output logic [5:0] secs,
  output logic       minute_roll
);
  assign minute_roll = tick & (secs == 6'd59);
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)    secs <= 6'd0;
    else if (tick) secs <= (secs == 6'd59) ? 6'd0 : secs + 6'd1;
  end
endmodule

module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       enable,
  output logic [5:0] secs,
  output logic       minute_roll
);
  logic tick;
  tick_gen    u_tg (.*);
  sec_counter u_sc (.*);
endmodule
