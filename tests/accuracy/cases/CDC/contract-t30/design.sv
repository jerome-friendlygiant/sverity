module top_cdc_rfc1_t30(input logic src_clk, dst_clk, rst_n,
 input logic macro_clk,
 input logic mode,
 output logic alarm,
 output logic dst_alarm
);
    (* black_box *) status_macro u_status(.clk(macro_clk),.mode(mode),.alarm(alarm)); logic a1,a2; 
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin a1<=0;a2<=0;dst_alarm<=0;end else begin a1<=alarm;a2<=a1;dst_alarm<=a2;end
endmodule

(* black_box *) module status_macro(input logic clk, mode, output logic alarm);
endmodule
