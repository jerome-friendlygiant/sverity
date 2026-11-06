module top(input logic clk,
  rst_n,
  take,
  
           output logic [3:0] credit,
  sampled_credit);

  always_ff @(posedge clk) begin
    if (!rst_n) credit = 4'd8;
    else if (take) credit = credit - 1'b1;
  end
  always_ff @(posedge clk) begin
    if (!rst_n) sampled_credit <= 4'd8;
    else sampled_credit <= credit;
  end

endmodule
