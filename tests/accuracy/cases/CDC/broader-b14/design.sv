module case_ib01(input logic clk, input logic rst_n, input logic irq_pin, output logic irq);
 logic a,b; always_ff @(posedge clk or negedge rst_n) if(!rst_n) begin a<=0;b<=0;irq<=0;end else begin a<=irq_pin;b<=a;irq<=b;end
endmodule
